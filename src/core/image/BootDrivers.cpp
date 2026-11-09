#include "core/image/BootDrivers.h"

#include "core/image/OffReg.h"
#include "core/system/Handle.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

namespace wl::core {

namespace {

std::mutex g_scanLock;
std::uint64_t g_scanCount = 0; // scopes ever opened
std::uint64_t g_scanOpen = 0;  // the open one's number
int g_scanDepth = 0;

} // namespace

ImageScanScope::ImageScanScope() {
    const std::scoped_lock guard(g_scanLock);
    if (g_scanDepth++ == 0) {
        g_scanOpen = ++g_scanCount;
    }
}

ImageScanScope::~ImageScanScope() {
    const std::scoped_lock guard(g_scanLock);
    if (--g_scanDepth == 0) {
        g_scanOpen = 0;
    }
}

std::uint64_t imageScanGeneration() {
    const std::scoped_lock guard(g_scanLock);
    return g_scanOpen;
}

FileStamp fileStamp(const std::filesystem::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) {
        return {};
    }
    return {(static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
            (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime};
}

namespace {

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

std::wstring regString(const std::vector<std::uint8_t>& data) {
    std::wstring text(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
    if (const auto end = text.find(L'\0'); end != std::wstring::npos) {
        text.resize(end);
    }
    return text;
}

bool startsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// Load order groups of the boot path. The loader takes the boot file system by its group (Ntfs is
// Start 3); a storage or bus driver becomes boot start on the PC whose system disk it drives
// (stornvme, storahci: Start 3 in the image).
constexpr std::wstring_view kBootGroups[] = {
    L"boot bus extender", L"system bus extender", L"scsi miniport", L"scsi port", L"scsi class",
    L"primary disk",      L"boot file system",    L"filter",        L"pnp filter",
};

// "c:\…" → "…" (the drive is the image's root offline).
bool stripDrive(std::wstring& path) {
    if (path.size() >= 3 && std::iswalpha(path[0]) != 0 && path[1] == L':' && path[2] == L'\\') {
        path.erase(0, 3);
        return true;
    }
    return false;
}

// A service's command line → its program: "%SystemRoot%\system32\svchost.exe -k netsvcs -p" →
// "%SystemRoot%\system32\svchost.exe"; a quoted path without its quotes.
std::wstring programPath(std::wstring_view command) {
    std::wstring text(command);
    while (!text.empty() && text.front() == L' ') {
        text.erase(text.begin());
    }
    if (!text.empty() && text.front() == L'"') {
        const auto end = text.find(L'"', 1);
        return end == std::wstring::npos ? std::wstring{} : text.substr(1, end - 1);
    }
    const auto exe = lower(text).find(L".exe");
    return exe == std::wstring::npos ? text : text.substr(0, exe + 4);
}

// A file read at offsets (nothing past its end).
class FileBytes {
public:
    explicit FileBytes(const std::filesystem::path& file)
        : m_handle(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)) {
        LARGE_INTEGER size{};
        if (m_handle && GetFileSizeEx(m_handle.get(), &size) && size.QuadPart > 0) {
            m_size = static_cast<std::uint64_t>(size.QuadPart);
        }
    }
    [[nodiscard]] std::uint64_t size() const noexcept { return m_size; }
    [[nodiscard]] bool read(std::uint64_t offset, void* out, std::size_t count) const {
        if (!m_handle || offset > m_size || count > m_size - offset || count > 0x7FFFFFFF) {
            return false;
        }
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(offset);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD got = 0;
        return ReadFile(m_handle.get(), out, static_cast<DWORD>(count), &got, &at) != 0 && got == count;
    }

private:
    UniqueHandle m_handle;
    std::uint64_t m_size = 0;
};

// The parts of a PE image the import walk reads.
struct PeLayout {
    std::uint32_t sizeOfHeaders = 0;
    IMAGE_DATA_DIRECTORY imports{};
    IMAGE_DATA_DIRECTORY delayImports{};
    std::vector<IMAGE_SECTION_HEADER> sections;

    // A relative virtual address as a file offset; nullopt outside the file's data.
    [[nodiscard]] std::optional<std::uint64_t> offsetOf(std::uint32_t rva) const {
        if (rva < sizeOfHeaders) {
            return rva;
        }
        for (const auto& s : sections) {
            const std::uint32_t span = std::max<std::uint32_t>(s.Misc.VirtualSize, s.SizeOfRawData);
            if (rva >= s.VirtualAddress && rva - s.VirtualAddress < span) {
                const std::uint32_t delta = rva - s.VirtualAddress;
                if (delta >= s.SizeOfRawData) {
                    return std::nullopt;
                }
                return static_cast<std::uint64_t>(s.PointerToRawData) + delta;
            }
        }
        return std::nullopt;
    }
};

template <class Optional>
void readDirectories(const FileBytes& bytes, std::uint64_t at, std::uint16_t size, PeLayout& pe) {
    Optional header{};
    if (!bytes.read(at, &header, std::min<std::size_t>(sizeof header, size))) {
        return;
    }
    pe.sizeOfHeaders = header.SizeOfHeaders;
    const auto has = [&](std::uint32_t entry) {
        return header.NumberOfRvaAndSizes > entry && size >= offsetof(Optional, DataDirectory) + (entry + 1) * sizeof(IMAGE_DATA_DIRECTORY);
    };
    if (has(IMAGE_DIRECTORY_ENTRY_IMPORT)) {
        pe.imports = header.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    }
    if (has(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)) {
        pe.delayImports = header.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    }
}

// A NUL-terminated ASCII name at a relative virtual address; empty when it is not in the file.
std::wstring nameAt(const FileBytes& bytes, const PeLayout& pe, std::uint32_t rva) {
    const auto at = pe.offsetOf(rva);
    if (!at || *at >= bytes.size()) {
        return {};
    }
    char name[260] = {};
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(sizeof name - 1, bytes.size() - *at));
    if (!bytes.read(*at, name, count)) {
        return {};
    }
    const std::string_view text(name, strnlen(name, count));
    return {text.begin(), text.end()};
}

std::optional<PeLayout> readPe(const FileBytes& bytes) {
    IMAGE_DOS_HEADER dos{};
    if (!bytes.read(0, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        return std::nullopt;
    }
    const auto nt = static_cast<std::uint64_t>(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER file{};
    if (!bytes.read(nt, &signature, sizeof signature) || signature != IMAGE_NT_SIGNATURE ||
        !bytes.read(nt + sizeof signature, &file, sizeof file) || file.NumberOfSections > 96) {
        return std::nullopt;
    }
    const std::uint64_t optional = nt + sizeof signature + sizeof file;
    WORD magic = 0;
    if (!bytes.read(optional, &magic, sizeof magic)) {
        return std::nullopt;
    }
    PeLayout pe;
    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        readDirectories<IMAGE_OPTIONAL_HEADER64>(bytes, optional, file.SizeOfOptionalHeader, pe);
    } else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        readDirectories<IMAGE_OPTIONAL_HEADER32>(bytes, optional, file.SizeOfOptionalHeader, pe);
    } else {
        return std::nullopt;
    }
    pe.sections.resize(file.NumberOfSections);
    if (!pe.sections.empty() &&
        !bytes.read(optional + file.SizeOfOptionalHeader, pe.sections.data(), pe.sections.size() * sizeof(IMAGE_SECTION_HEADER))) {
        return std::nullopt;
    }
    return pe;
}

} // namespace

std::wstring driverImageFile(std::wstring_view imagePath, std::wstring_view service) {
    std::wstring path = lower(imagePath);
    while (!path.empty() && (path.front() == L' ' || path.front() == L'"')) {
        path.erase(path.begin());
    }
    while (!path.empty() && (path.back() == L' ' || path.back() == L'"')) {
        path.pop_back();
    }
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.empty()) {
        if (service.empty()) {
            return {};
        }
        path = L"system32\\drivers\\" + lower(service) + L".sys"; // what the loader uses then
    }
    if (startsWith(path, L"\\systemroot\\")) {
        path = L"windows\\" + path.substr(12);
    } else if (startsWith(path, L"%systemroot%\\")) {
        path = L"windows\\" + path.substr(13);
    } else if (startsWith(path, L"%windir%\\")) {
        path = L"windows\\" + path.substr(9);
    } else if (startsWith(path, L"\\??\\") || startsWith(path, L"\\\\?\\")) {
        path.erase(0, 4);
        if (!stripDrive(path)) {
            return {}; // a device path, a volume GUID …
        }
    } else if (stripDrive(path)) {
        // "c:\windows\…": below the image root as it is
    } else if (path.front() == L'\\') {
        path.erase(path.begin()); // rooted: the system drive
    } else {
        path = L"windows\\" + path; // relative: to SystemRoot
    }
    if (path.empty() || path.find(L'%') != std::wstring::npos || path.find(L':') != std::wstring::npos) {
        return {};
    }
    // No empty, "." or ".." component: the path must name one file below the root.
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find(L'\\', start), path.size());
        const std::wstring_view part(path.data() + start, end - start);
        if (part.empty() || part == L"." || part == L"..") {
            return {};
        }
        start = end + 1;
    }
    return path;
}

std::wstring imageRelativePath(const std::filesystem::path& path, const std::filesystem::path& mountDir) {
    auto plain = [](const std::filesystem::path& p) {
        std::wstring text = lower(p.wstring());
        if (startsWith(text, L"\\\\?\\")) {
            text.erase(0, 4);
        }
        std::replace(text.begin(), text.end(), L'/', L'\\');
        return text;
    };
    const std::wstring full = plain(path);
    std::wstring root = plain(mountDir);
    while (!root.empty() && root.back() == L'\\') {
        root.pop_back();
    }
    if (root.empty() || full.size() <= root.size() + 1 || !startsWith(full, root) || full[root.size()] != L'\\') {
        return {};
    }
    return full.substr(root.size() + 1);
}

bool isBootFileType(const std::filesystem::path& path) {
    const auto extension = lower(path.extension().wstring());
    return extension == L".sys" || extension == L".dll" || extension == L".exe" || extension == L".efi" || extension == L".nls";
}

std::vector<std::filesystem::path> bootFilesUnder(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> out;
    const auto isDriver = [](const std::filesystem::path& p) { return isBootFileType(p); };
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(status)) {
        return out;
    }
    if (std::filesystem::is_regular_file(status)) {
        if (isDriver(path)) {
            out.push_back(path);
        }
        return out;
    }
    if (!std::filesystem::is_directory(status)) {
        return out;
    }
    for (std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && isDriver(it->path())) {
            out.push_back(it->path());
        }
    }
    return out;
}

namespace {

std::vector<std::wstring> importNames(const FileBytes& bytes, const PeLayout& pe) {
    std::vector<std::wstring> out;
    const auto table = pe.imports.VirtualAddress == 0 ? std::nullopt : pe.offsetOf(pe.imports.VirtualAddress);
    if (!table) {
        return out;
    }
    for (std::uint64_t i = 0; i < 4096; ++i) {
        IMAGE_IMPORT_DESCRIPTOR entry{};
        if (!bytes.read(*table + i * sizeof entry, &entry, sizeof entry) || entry.Name == 0) {
            break;
        }
        if (auto name = nameAt(bytes, pe, entry.Name); !name.empty()) {
            out.push_back(std::move(name));
        }
    }
    return out;
}

std::vector<std::wstring> delayImportNames(const FileBytes& bytes, const PeLayout& pe) {
    std::vector<std::wstring> out;
    const auto table = pe.delayImports.VirtualAddress == 0 ? std::nullopt : pe.offsetOf(pe.delayImports.VirtualAddress);
    if (!table) {
        return out;
    }
    for (std::uint64_t i = 0; i < 4096; ++i) {
        IMAGE_DELAYLOAD_DESCRIPTOR entry{};
        if (!bytes.read(*table + i * sizeof entry, &entry, sizeof entry) || entry.DllNameRVA == 0) {
            break;
        }
        // RvaBased clear: the old (VC6) layout with virtual addresses; Windows' own files have none.
        if (entry.Attributes.RvaBased == 0) {
            continue;
        }
        if (auto name = nameAt(bytes, pe, entry.DllNameRVA); !name.empty()) {
            out.push_back(std::move(name));
        }
    }
    return out;
}

} // namespace

std::vector<std::wstring> peImports(const std::filesystem::path& file) {
    const FileBytes bytes(file);
    const auto pe = readPe(bytes);
    return pe ? importNames(bytes, *pe) : std::vector<std::wstring>{};
}

std::vector<std::wstring> peDelayImports(const std::filesystem::path& file) {
    const FileBytes bytes(file);
    const auto pe = readPe(bytes);
    return pe ? delayImportNames(bytes, *pe) : std::vector<std::wstring>{};
}

Result<ApiSetSchema> readApiSetSchema(const std::filesystem::path& file) {
    const FileBytes bytes(file);
    const auto pe = readPe(bytes);
    if (!pe) {
        return fail(ErrorCode::ParseError, L"not a PE image", file.wstring());
    }
    const auto section = std::ranges::find_if(pe->sections, [](const IMAGE_SECTION_HEADER& s) {
        return std::memcmp(s.Name, ".apiset", 7) == 0 && s.Name[7] == 0;
    });
    if (section == pe->sections.end()) {
        return fail(ErrorCode::ParseError, L"no .apiset section", file.wstring());
    }
    std::vector<std::uint8_t> data(section->SizeOfRawData);
    if (data.size() < 28 || data.size() > (16u << 20) || !bytes.read(section->PointerToRawData, data.data(), data.size())) {
        return fail(ErrorCode::ParseError, L"API set schema not readable", file.wstring());
    }
    const auto u32 = [&](std::uint64_t at) -> std::optional<std::uint32_t> {
        if (at > data.size() || data.size() - at < 4) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        std::memcpy(&value, data.data() + at, 4);
        return value;
    };
    const auto text = [&](std::uint32_t offset, std::uint32_t length) -> std::optional<std::wstring> {
        if (offset > data.size() || length > data.size() - offset || length % 2 != 0) {
            return std::nullopt;
        }
        std::wstring out(length / 2, L'\0');
        std::memcpy(out.data(), data.data() + offset, length);
        return lower(out);
    };
    const auto version = u32(0);
    const auto count = u32(12);
    const auto entries = u32(16);
    if (!version || *version != 6) {
        return fail(ErrorCode::Unsupported, std::format(L"API set schema version {}", version.value_or(0)), file.wstring());
    }
    if (!count || !entries || *count > 100000) {
        return fail(ErrorCode::ParseError, L"API set schema header", file.wstring());
    }
    ApiSetSchema schema;
    for (std::uint32_t i = 0; i < *count; ++i) {
        const std::uint64_t at = *entries + static_cast<std::uint64_t>(i) * 24;
        const auto nameOffset = u32(at + 4);
        const auto nameLength = u32(at + 8);
        const auto hashedLength = u32(at + 12);
        const auto valueOffset = u32(at + 16);
        const auto valueCount = u32(at + 20);
        if (!nameOffset || !nameLength || !hashedLength || !valueOffset || !valueCount || *valueCount > 64) {
            return fail(ErrorCode::ParseError, L"API set schema entry", file.wstring());
        }
        auto name = text(*nameOffset, *nameLength);
        if (!name) {
            return fail(ErrorCode::ParseError, L"API set schema name", file.wstring());
        }
        // The hashed part: the name up to its last hyphen (any minor version resolves the same way).
        if (*hashedLength / 2 <= name->size()) {
            name->resize(*hashedLength / 2);
        }
        auto& hosts = schema[*name];
        for (std::uint32_t j = 0; j < *valueCount; ++j) {
            const std::uint64_t value = *valueOffset + static_cast<std::uint64_t>(j) * 20;
            const auto hostOffset = u32(value + 12);
            const auto hostLength = u32(value + 16);
            if (!hostOffset || !hostLength) {
                return fail(ErrorCode::ParseError, L"API set schema value", file.wstring());
            }
            if (*hostLength == 0) {
                continue;
            }
            auto host = text(*hostOffset, *hostLength);
            if (host && !host->empty() && std::ranges::find(hosts, *host) == hosts.end()) {
                hosts.push_back(std::move(*host));
            }
        }
    }
    return schema;
}

std::vector<std::wstring> importHosts(const ApiSetSchema& schema, std::wstring_view dll) {
    std::wstring name = lower(dll);
    if (!startsWith(name, L"api-") && !startsWith(name, L"ext-")) {
        return {name};
    }
    if (name.size() > 4 && name.ends_with(L".dll")) {
        name.resize(name.size() - 4);
    }
    if (const auto dash = name.rfind(L'-'); dash != std::wstring::npos) {
        name.resize(dash);
    }
    const auto found = schema.find(name);
    return found == schema.end() ? std::vector<std::wstring>{} : found->second;
}

std::vector<std::wstring> multiStrings(const std::vector<std::uint8_t>& data) {
    std::vector<std::wstring> out;
    const std::wstring_view all(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
    std::size_t start = 0;
    while (start < all.size()) {
        std::size_t end = all.find(L'\0', start);
        if (end == std::wstring_view::npos) {
            end = all.size();
        }
        if (end > start) {
            out.emplace_back(all.substr(start, end - start));
        }
        start = end + 1;
    }
    return out;
}

static Result<std::vector<BootDriver>> readBootDrivers(const std::filesystem::path& mountDir) {
    auto hive = OffRegKey::openHive(mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM");
    if (!hive) {
        return std::unexpected(hive.error());
    }
    std::map<std::wstring, BootDriver> found; // by lower-case service name
    for (const auto& set : hive->subkeys()) {
        if (!startsWith(lower(set), L"controlset")) {
            continue;
        }
        std::set<std::wstring> filters;
        const auto classes = hive->open(set + L"\\Control\\Class");
        for (const auto& guid : classes.subkeys()) {
            const auto deviceClass = classes.open(guid);
            for (const wchar_t* value : {L"UpperFilters", L"LowerFilters"}) {
                for (const auto& name : multiStrings(deviceClass.bytes(value))) {
                    filters.insert(lower(name));
                }
            }
        }
        const auto services = hive->open(set + L"\\Services");
        for (const auto& name : services.subkeys()) {
            const auto key = services.open(name);
            const auto type = key.dword(L"Type");
            if (type && (*type & 0x30) != 0) {
                // D-117: a Win32 service Windows starts by itself (Start 2): its service DLL, or its program.
                const auto start = key.dword(L"Start");
                if (!start || *start != 2) {
                    continue;
                }
                std::wstring binary = regString(key.open(L"Parameters").bytes(L"ServiceDll"));
                if (binary.empty()) {
                    binary = regString(key.bytes(L"ServiceDll"));
                }
                if (binary.empty()) {
                    binary = programPath(regString(key.bytes(L"ImagePath")));
                }
                auto file = binary.empty() ? std::wstring{} : driverImageFile(binary, L"");
                // Windows' own services only: a program under Program Files that does not start (Edge's
                // updater after its removal) does not keep Windows from starting.
                if (startsWith(file, L"windows\\")) {
                    auto& entry = found[lower(name)];
                    entry.service = name;
                    entry.file = std::move(file);
                    entry.early = true;
                }
                continue;
            }
            // Kernel driver 1, file system driver 2, recognizer 8.
            if (!type || (*type & 0x0B) == 0) {
                continue;
            }
            const auto start = key.dword(L"Start");
            const auto group = lower(regString(key.bytes(L"Group")));
            const bool early = (start && *start <= 1) ||
                               std::ranges::find(kBootGroups, std::wstring_view(group)) != std::end(kBootGroups);
            const bool filter = filters.contains(lower(name));
            if (!early && !filter) {
                continue;
            }
            auto file = driverImageFile(regString(key.bytes(L"ImagePath")), name);
            if (file.empty()) {
                continue;
            }
            auto& driver = found[lower(name)];
            driver.service = name;
            driver.file = std::move(file);
            driver.early = driver.early || early;
            driver.filter = driver.filter || filter;
        }
        // The code pages the loader reads (ACP 1254 → c_1254.nls): a missing one stops it too.
        const auto codePages = hive->open(set + L"\\Control\\Nls\\CodePage");
        for (const wchar_t* which : {L"ACP", L"OEMCP"}) {
            const auto page = regString(codePages.bytes(which));
            const auto file = page.empty() ? std::wstring{} : regString(codePages.bytes(page.c_str()));
            if (file.empty() || file.find_first_of(L"\\/:%") != std::wstring::npos) {
                continue;
            }
            auto& entry = found[lower(L"nls\\codepage\\" + page)];
            entry.service = L"Nls\\CodePage\\" + page;
            entry.file = L"windows\\system32\\" + lower(file);
            entry.early = true;
        }
    }
    std::vector<BootDriver> out;
    out.reserve(found.size());
    for (auto& [_, driver] : found) {
        out.push_back(std::move(driver));
    }
    return out;
}

std::map<std::wstring, std::wstring> keptDriverFiles(const std::vector<BootDriver>& drivers,
                                                     const std::vector<std::wstring>& services) {
    std::set<std::wstring> removed;
    for (const auto& service : services) {
        removed.insert(lower(service));
    }
    std::map<std::wstring, std::wstring> kept;
    for (const auto& driver : drivers) {
        if (driver.filter || (driver.early && !removed.contains(lower(driver.service)))) {
            kept.emplace(driver.file, driver.service);
        }
    }
    return kept;
}

std::vector<std::wstring> keptFilterServices(const std::vector<BootDriver>& drivers, const std::vector<std::wstring>& services) {
    std::vector<std::wstring> kept;
    for (const auto& service : services) {
        const auto name = lower(service);
        if (std::ranges::any_of(drivers, [&](const BootDriver& d) { return d.filter && lower(d.service) == name; })) {
            kept.push_back(name);
        }
    }
    return kept;
}

const std::vector<std::wstring>& loaderFiles() {
    // The kernel and what the loader picks for it (debugger transport, microcode, code integrity),
    // the API set schema, the default language table, the loaders themselves.
    static const std::vector<std::wstring> files = {
        L"windows\\system32\\ntoskrnl.exe",       L"windows\\system32\\hal.dll",
        L"windows\\system32\\kdcom.dll",          L"windows\\system32\\kd.dll",
        L"windows\\system32\\kdstub.dll",         L"windows\\system32\\kdnet.dll",
        L"windows\\system32\\ci.dll",             L"windows\\system32\\mcupdate_genuineintel.dll",
        L"windows\\system32\\mcupdate_authenticamd.dll", L"windows\\system32\\apisetschema.dll",
        L"windows\\system32\\l_intl.nls",         L"windows\\system32\\winload.efi",
        L"windows\\system32\\winload.exe",        L"windows\\system32\\winresume.efi",
        L"windows\\system32\\winresume.exe",      L"windows\\system32\\securekernel.exe",
        L"windows\\system32\\skci.dll",
    };
    return files;
}

const std::vector<std::wstring>& sessionPrograms() {
    // The session and sign-in chain, the shell, Setup's own steps (specialize, OOBE) and what they
    // and WinLove's answer files start (cmd, reg, PowerShell).
    static const std::vector<std::wstring> files = {
        L"windows\\system32\\smss.exe",       L"windows\\system32\\csrss.exe",       L"windows\\system32\\csrsrv.dll",
        L"windows\\system32\\basesrv.dll",    L"windows\\system32\\winsrv.dll",      L"windows\\system32\\sxssrv.dll",
        L"windows\\system32\\wininit.exe",    L"windows\\system32\\winlogon.exe",    L"windows\\system32\\services.exe",
        L"windows\\system32\\lsass.exe",      L"windows\\system32\\lsaiso.exe",      L"windows\\system32\\svchost.exe",
        L"windows\\system32\\logonui.exe",
        L"windows\\system32\\dwm.exe",        L"windows\\system32\\userinit.exe",    L"windows\\explorer.exe",
        L"windows\\system32\\sihost.exe",     L"windows\\system32\\fontdrvhost.exe", L"windows\\system32\\ctfmon.exe",
        L"windows\\system32\\taskhostw.exe",  L"windows\\system32\\runtimebroker.exe", L"windows\\system32\\dllhost.exe",
        L"windows\\system32\\conhost.exe",    L"windows\\system32\\cmd.exe",         L"windows\\system32\\rundll32.exe",
        L"windows\\system32\\reg.exe",        L"windows\\system32\\consent.exe",     L"windows\\system32\\setupcl.exe",
        L"windows\\system32\\oobe\\windeploy.exe", L"windows\\system32\\oobe\\setup.exe",
        L"windows\\system32\\oobe\\msoobe.exe",    L"windows\\system32\\oobe\\oobeldr.exe",
        L"windows\\system32\\windowspowershell\\v1.0\\powershell.exe",
    };
    return files;
}

namespace {

// A module's imports, read once per process for an unchanged file (removal asks for every component).
// nullopt: no such file.
std::optional<std::vector<std::wstring>> cachedImports(const std::filesystem::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return std::nullopt;
    }
    struct Entry {
        std::uint64_t size = 0;
        std::uint64_t written = 0;
        std::vector<std::wstring> imports;
    };
    static std::mutex lock;
    static std::map<std::wstring, Entry> cache;
    const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    const std::uint64_t written =
        (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    const std::wstring key = lower(file.wstring());
    {
        const std::scoped_lock guard(lock);
        if (const auto found = cache.find(key); found != cache.end() && found->second.size == size && found->second.written == written) {
            return found->second.imports;
        }
    }
    auto imports = peImports(file);
    const std::scoped_lock guard(lock);
    cache[key] = Entry{size, written, imports};
    return imports;
}

// namedModules, read once per process for an unchanged file (explorer.exe is megabytes).
std::vector<std::wstring> cachedNamedModules(const std::filesystem::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return {};
    }
    static std::mutex lock;
    static std::map<std::wstring, std::pair<std::uint64_t, std::vector<std::wstring>>> cache;
    const std::uint64_t stamp =
        ((static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime) ^
        ((static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
    const std::wstring key = lower(file.wstring());
    {
        const std::scoped_lock guard(lock);
        if (const auto found = cache.find(key); found != cache.end() && found->second.first == stamp) {
            return found->second.second;
        }
    }
    auto names = namedModules(file);
    const std::scoped_lock guard(lock);
    cache[key] = {stamp, names};
    return names;
}

} // namespace

std::vector<std::wstring> namedModules(const std::filesystem::path& file) {
    const FileBytes bytes(file);
    if (bytes.size() == 0 || bytes.size() > (64ull << 20)) {
        return {};
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes.size()));
    if (!bytes.read(0, data.data(), data.size())) {
        return {};
    }
    const auto nameChar = [](unsigned c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    };
    std::set<std::wstring> found;
    // Text that ends in ".dll" and then a terminating NUL, in single bytes or UTF-16; the name is
    // the run of name characters before it.
    const auto take = [&](std::size_t end, std::size_t step) {
        std::wstring name;
        std::size_t at = end;
        while (at >= step && name.size() < 64) {
            const std::size_t prev = at - step;
            const unsigned c = data[prev];
            if (!nameChar(c) || (step == 2 && data[prev + 1] != 0)) {
                break;
            }
            name.insert(name.begin(), static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))));
            at = prev;
        }
        if (name.size() > 4 && (name.ends_with(L".dll") || name.ends_with(L".exe")) && name.front() != L'.') {
            found.insert(std::move(name));
        }
    };
    // "dll" / "exe" (any case) at data[at], data[at + step], data[at + 2 * step].
    const auto extensionAt = [&](std::size_t at, std::size_t step) {
        const unsigned a = data[at] | 0x20U;
        const unsigned b = data[at + step] | 0x20U;
        const unsigned c = data[at + 2 * step] | 0x20U;
        return (a == 'd' && b == 'l' && c == 'l') || (a == 'e' && b == 'x' && c == 'e');
    };
    for (std::size_t i = 4; i < data.size(); ++i) {
        // ".dll\0" / ".exe\0"
        if (data[i] == 0 && data[i - 4] == '.' && extensionAt(i - 3, 1)) {
            take(i, 1);
        }
        // ".\0d\0l\0l\0" then "\0\0" at i (UTF-16)
        if (i >= 9 && i + 1 < data.size() && data[i] == 0 && data[i + 1] == 0 && data[i - 1] == 0 && data[i - 3] == 0 &&
            data[i - 5] == 0 && data[i - 7] == 0 && data[i - 8] == '.' && extensionAt(i - 6, 2)) {
            take(i, 2);
        }
    }
    return {found.begin(), found.end()};
}

std::vector<std::wstring> sysprepModules(const std::filesystem::path& mountDir) {
    std::set<std::wstring> out;
    std::error_code ec;
    const auto folder = mountDir / L"Windows" / L"System32" / L"Sysprep" / L"ActionFiles";
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (lower(it->path().extension().wstring()) != L".xml") {
            continue;
        }
        const FileBytes bytes(it->path());
        if (bytes.size() == 0 || bytes.size() > (8u << 20)) {
            continue;
        }
        std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes.size()));
        if (!bytes.read(0, data.data(), data.size())) {
            continue;
        }
        std::wstring text;
        if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
            text.assign(reinterpret_cast<const wchar_t*>(data.data() + 2), (data.size() - 2) / 2);
        } else {
            text.assign(data.begin(), data.end()); // UTF-8: the attribute is ASCII
        }
        text = lower(text);
        for (std::size_t at = text.find(L"modulename=\""); at != std::wstring::npos; at = text.find(L"modulename=\"", at + 1)) {
            const std::size_t start = at + 12;
            const std::size_t stop = text.find(L'"', start);
            if (stop == std::wstring::npos) {
                break;
            }
            std::wstring path = text.substr(start, stop - start);
            for (const auto& [macro, root] : {std::pair<std::wstring_view, std::wstring_view>{L"$(runtime.system32)\\", L"windows\\system32\\"},
                                              {L"$(runtime.windows)\\", L"windows\\"}}) {
                if (startsWith(path, macro)) {
                    path = std::wstring(root) + path.substr(macro.size());
                }
            }
            if (startsWith(path, L"windows\\") && path.find_first_of(L"$%:") == std::wstring::npos && path.find(L"..") == std::wstring::npos) {
                out.insert(std::move(path));
            }
        }
    }
    return {out.begin(), out.end()};
}

// The image's API set schema, kept while a scan scope is open and the file is unchanged (D-128).
static Result<ApiSetSchema> schemaOf(const std::filesystem::path& mountDir) {
    const auto file = mountDir / L"Windows" / L"System32" / L"apisetschema.dll";
    const std::uint64_t scan = imageScanGeneration();
    if (scan == 0) {
        return readApiSetSchema(file);
    }
    struct Kept {
        std::uint64_t scan = 0;
        std::wstring file;
        FileStamp stamp;
        std::optional<Result<ApiSetSchema>> schema;
    };
    static std::mutex lock;
    static Kept kept;
    const FileStamp stamp = fileStamp(file);
    const std::wstring name = lower(file.wstring());
    const std::scoped_lock guard(lock);
    if (!kept.schema || kept.scan != scan || kept.file != name || kept.stamp != stamp || stamp.size == 0) {
        kept = Kept{scan, name, stamp, readApiSetSchema(file)};
    }
    return *kept.schema;
}

Result<std::vector<BootDriver>> findBootDrivers(const std::filesystem::path& mountDir) {
    const std::uint64_t scan = imageScanGeneration();
    if (scan == 0) {
        return readBootDrivers(mountDir);
    }
    struct Kept {
        std::uint64_t scan = 0;
        std::wstring root;
        FileStamp stamp;
        std::optional<Result<std::vector<BootDriver>>> drivers;
    };
    static std::mutex lock;
    static Kept kept;
    const FileStamp stamp = fileStamp(mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM");
    const std::wstring root = lower(mountDir.wstring());
    const std::scoped_lock guard(lock);
    if (!kept.drivers || kept.scan != scan || kept.root != root || kept.stamp != stamp || stamp.size == 0) {
        kept = Kept{scan, root, stamp, readBootDrivers(mountDir)};
    }
    return *kept.drivers;
}

static Result<std::map<std::wstring, std::wstring>> readKeptBootFiles(const std::filesystem::path& mountDir,
                                                                      const std::vector<BootDriver>& drivers,
                                                                      const std::vector<std::wstring>& services) {
    auto schema = schemaOf(mountDir);
    if (!schema) {
        return std::unexpected(schema.error());
    }
    const auto isFile = [&](const std::wstring& relative) {
        const DWORD attributes = GetFileAttributesW((mountDir / relative).c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    };
    auto kept = keptDriverFiles(drivers, services);
    for (const auto& file : loaderFiles()) {
        if (isFile(file)) {
            kept.emplace(file, L"the boot loader");
        }
    }
    // Looked through for names: the session programs (the DLLs they name stay) and Setup's sysprep
    // modules (the DLLs and the programs they name stay; such a program is looked through as well:
    // ProvSysprep.dll → provtool.exe). A program a session program names is not kept: winlogon → …
    // → taskmgr.exe → Windows.WARP.JITService.exe kept what the WARP removal takes (lab, 2026-11-08).
    std::set<std::wstring> programs(sessionPrograms().begin(), sessionPrograms().end());
    std::set<std::wstring> setupChain;
    for (const auto& file : programs) {
        if (isFile(file)) {
            kept.emplace(file, L"Windows starts it");
        }
    }
    for (auto& file : sysprepModules(mountDir)) {
        if (isFile(file)) {
            kept.emplace(file, L"Setup runs it (Sysprep\\ActionFiles)");
            setupChain.insert(file);
            programs.insert(std::move(file));
        }
    }
    // The API set hosts (below): what one loads by name is its dependency too when it names only a few
    // DLLs (srpapi.dll loads appidapi.dll: without it PowerShell still ran in Constrained Language,
    // lab 2026-11-08). One that names many is a catalog, not a dependency list (KernelBase.dll names
    // 546 System32 DLLs, shell32.dll 90): measured on 26100, at most 10 keeps 45 more files.
    constexpr std::size_t kHostNamedLimit = 10;
    std::set<std::wstring> apiHosts;
    for (const auto& [_, hosts] : *schema) {
        for (const auto& host : hosts) {
            if (!host.empty() && host.find_first_of(L"\\/:%") == std::wstring::npos) {
                apiHosts.insert(L"windows\\system32\\" + host);
                apiHosts.insert(L"windows\\system32\\drivers\\" + host);
            }
        }
    }
    std::vector<std::wstring> todo;
    for (const auto& [file, _] : kept) {
        todo.push_back(file);
    }
    std::set<std::wstring> walked;
    const auto drain = [&] {
        while (!todo.empty()) {
            const std::wstring file = std::move(todo.back());
            todo.pop_back();
            if (!walked.insert(file).second) {
                continue;
            }
            const std::filesystem::path module(file);
            const std::wstring importer = module.filename().wstring();
            const std::wstring folder = module.has_parent_path() ? module.parent_path().wstring() + L"\\" : std::wstring{};
            const auto imports = cachedImports(mountDir / file);
            if (!imports) {
                continue;
            }
            // A session program also loads modules and starts programs by name (winlogon.exe /
            // wininit.exe: utcapi.dll, without it the sign-in session never starts): kept like imports.
            if (programs.contains(file)) {
                for (const auto& name : cachedNamedModules(mountDir / file)) {
                    const bool program = name.ends_with(L".exe");
                    // A program a session program names (notepad.exe, taskmgr.exe, sethc.exe …) does
                    // not start the session: it may go. Setup's chain starts what it names.
                    if (program && !setupChain.contains(file)) {
                        continue;
                    }
                    for (const auto& where : {std::wstring(L"windows\\system32\\"), folder}) {
                        std::wstring candidate = where + name;
                        if (kept.contains(candidate) || !isFile(candidate)) {
                            continue;
                        }
                        kept.emplace(candidate, importer + (program ? L" starts it by name" : L" loads it by name"));
                        if (program && setupChain.contains(file)) {
                            setupChain.insert(candidate);
                            programs.insert(candidate);
                        }
                        todo.push_back(std::move(candidate));
                    }
                }
            }
            if (apiHosts.contains(file)) {
                std::vector<std::wstring> named;
                for (const auto& name : cachedNamedModules(mountDir / file)) {
                    if (name.ends_with(L".dll") && isFile(L"windows\\system32\\" + name)) {
                        named.push_back(L"windows\\system32\\" + name);
                    }
                }
                if (named.size() <= kHostNamedLimit) {
                    for (auto& candidate : named) {
                        if (!kept.contains(candidate)) {
                            kept.emplace(candidate, importer + L" loads it by name");
                            todo.push_back(std::move(candidate));
                        }
                    }
                }
            }
            for (const auto& dll : *imports) {
                for (const auto& host : importHosts(*schema, dll)) {
                    if (host.empty() || host.find_first_of(L"\\/:%") != std::wstring::npos) {
                        continue;
                    }
                    // The loader looks in System32, System32\drivers and the importer's own folder.
                    for (const auto& where : {std::wstring(L"windows\\system32\\"), std::wstring(L"windows\\system32\\drivers\\"), folder}) {
                        std::wstring candidate = where + host;
                        if (!kept.contains(candidate) && isFile(candidate)) {
                            kept.emplace(candidate, importer + L" imports it");
                            todo.push_back(std::move(candidate));
                        }
                    }
                }
            }
        }
    };
    drain();
    // Then every host the API set schema names (after the walk, so a module some boot file imports
    // keeps that reason): Windows' own contract list. Even an "ext-" contract's host is needed in
    // practice: KernelBase answers application control through srpapi.dll (the AppLocker removal took
    // it, PowerShell fell to Constrained Language and WinLove's own setup screens came up without a
    // word of text); winlogonext / wininitext / userinitext reach the sign-in chain this way, not
    // through an import table.
    for (const auto& [_, hosts] : *schema) {
        for (const auto& host : hosts) {
            if (host.empty() || host.find_first_of(L"\\/:%") != std::wstring::npos) {
                continue;
            }
            for (const wchar_t* where : {L"windows\\system32\\", L"windows\\system32\\drivers\\"}) {
                std::wstring candidate = std::wstring(where) + host;
                if (!kept.contains(candidate) && isFile(candidate)) {
                    kept.emplace(candidate, L"an API set host");
                    todo.push_back(std::move(candidate));
                }
            }
        }
    }
    drain();
    return kept;
}

namespace {

// D-118: a module removal looks through for importers.
struct ModuleFile {
    std::wstring path; // below the image root, lower case
    std::uint64_t size = 0;
    std::uint64_t written = 0;
};

bool isModuleName(std::wstring_view name) {
    static constexpr std::wstring_view kinds[] = {L".dll", L".exe", L".sys", L".drv", L".ocx", L".cpl",
                                                  L".ax",  L".scr", L".ime", L".tsp", L".acm"};
    const std::wstring low = lower(name);
    return std::ranges::any_of(kinds, [&](std::wstring_view kind) { return low.size() > kind.size() && low.ends_with(kind); });
}

} // namespace

Result<std::map<std::wstring, std::wstring>> keptBootFiles(const std::filesystem::path& mountDir,
                                                           const std::vector<BootDriver>& drivers,
                                                           const std::vector<std::wstring>& services) {
    const std::uint64_t scan = imageScanGeneration();
    if (scan == 0) {
        return readKeptBootFiles(mountDir, drivers, services);
    }
    // What stays depends on the drivers (the SYSTEM hive), the step's services and files that stay.
    std::vector<std::wstring> names;
    for (const auto& service : services) {
        names.push_back(lower(service));
    }
    std::ranges::sort(names);
    std::wstring key = std::to_wstring(drivers.size());
    for (const auto& name : names) {
        key += L"|" + name;
    }
    struct Kept {
        std::uint64_t scan = 0;
        std::wstring root;
        FileStamp stamp;
        std::map<std::wstring, Result<std::map<std::wstring, std::wstring>>> byServices;
    };
    static std::mutex lock;
    static Kept kept;
    const FileStamp stamp = fileStamp(mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM");
    const std::wstring root = lower(mountDir.wstring());
    const std::scoped_lock guard(lock);
    if (kept.scan != scan || kept.root != root || kept.stamp != stamp || stamp.size == 0) {
        kept = Kept{scan, root, stamp, {}};
    }
    if (const auto found = kept.byServices.find(key); found != kept.byServices.end()) {
        return found->second;
    }
    auto files = readKeptBootFiles(mountDir, drivers, services);
    kept.byServices.emplace(key, files);
    return files;
}

namespace {

// The modules in `folder` (and its subfolders when `deep`; links and junctions are not followed),
// named `prefix` + their lower-case names.
void listModules(const std::filesystem::path& folder, const std::wstring& prefix, bool deep, std::vector<ModuleFile>& out,
                 int depth = 0) {
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileExW((folder / L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                         FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (deep && depth < 8 && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                listModules(folder / name, prefix + lower(name) + L"\\", deep, out, depth + 1);
            }
            continue;
        }
        if (isModuleName(name)) {
            out.push_back({prefix + lower(name), (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
                           (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime});
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

struct ModuleImports {
    std::vector<std::wstring> imports; // lower case
    std::vector<std::wstring> delayed;
};

// A module's import and delay-load lists, read once per process for an unchanged file: a preset
// removes hundreds of components, and between two of them only the removed files change.
ModuleImports cachedModuleImports(const std::filesystem::path& file, std::uint64_t size, std::uint64_t written) {
    struct Entry {
        std::uint64_t size = 0;
        std::uint64_t written = 0;
        ModuleImports lists;
    };
    static std::mutex lock;
    static std::map<std::wstring, Entry> cache;
    const std::wstring key = lower(file.wstring());
    {
        const std::scoped_lock guard(lock);
        if (const auto found = cache.find(key); found != cache.end() && found->second.size == size && found->second.written == written) {
            return found->second.lists;
        }
    }
    ModuleImports lists;
    const FileBytes bytes(file);
    if (const auto pe = readPe(bytes)) {
        for (auto& name : importNames(bytes, *pe)) {
            lists.imports.push_back(lower(name));
        }
        for (auto& name : delayImportNames(bytes, *pe)) {
            lists.delayed.push_back(lower(name));
        }
    }
    const std::scoped_lock guard(lock);
    cache[key] = Entry{size, written, lists};
    return lists;
}

} // namespace

Result<std::map<std::wstring, std::wstring>> keptImportedFiles(const std::filesystem::path& mountDir, const std::vector<std::wstring>& going) {
    std::map<std::wstring, std::wstring> kept;
    std::set<std::wstring> goingSet;
    for (const auto& file : going) {
        if (!file.empty()) {
            goingSet.insert(lower(file));
        }
    }
    if (goingSet.empty()) {
        return kept;
    }
    auto schema = schemaOf(mountDir);
    if (!schema) {
        return std::unexpected(schema.error());
    }
    std::vector<ModuleFile> modules;
    const auto windows = mountDir / L"Windows";
    listModules(windows, L"windows\\", false, modules);
    listModules(windows / L"System32", L"windows\\system32\\", false, modules);
    listModules(windows / L"System32" / L"drivers", L"windows\\system32\\drivers\\", false, modules);
    listModules(windows / L"SysWOW64", L"windows\\syswow64\\", false, modules);
    listModules(windows / L"SystemApps", L"windows\\systemapps\\", true, modules);
    listModules(windows / L"ImmersiveControlPanel", L"windows\\immersivecontrolpanel\\", true, modules);

    // Each module's imports resolved to the files they may mean (and whether only on demand).
    const auto resolve = [&](const ModuleFile& module) {
        auto targets = std::make_shared<std::vector<std::pair<std::wstring, bool>>>();
        const ModuleImports lists = cachedModuleImports(mountDir / module.path, module.size, module.written);
        const std::size_t slash = module.path.rfind(L'\\');
        const std::wstring folder = module.path.substr(0, slash + 1);
        // A 32-bit module in SysWOW64 loads 32-bit modules: the file system redirects it there.
        const std::vector<std::wstring> where = startsWith(module.path, L"windows\\syswow64\\")
                                                    ? std::vector<std::wstring>{L"windows\\syswow64\\", folder}
                                                    : std::vector<std::wstring>{L"windows\\system32\\", L"windows\\system32\\drivers\\", folder};
        for (const bool delayed : {false, true}) {
            for (const auto& dll : delayed ? lists.delayed : lists.imports) {
                for (const auto& host : importHosts(*schema, dll)) {
                    if (host.empty() || host.find_first_of(L"\\/:%") != std::wstring::npos) {
                        continue;
                    }
                    for (const auto& dir : where) {
                        if (std::wstring candidate = dir + host; candidate != module.path) {
                            targets->emplace_back(std::move(candidate), delayed);
                        }
                    }
                }
            }
        }
        return std::shared_ptr<const std::vector<std::pair<std::wstring, bool>>>(std::move(targets));
    };
    // D-128: kept per module while a scan scope is open (the module and the schema unchanged).
    struct Resolved {
        std::uint64_t size = 0;
        std::uint64_t written = 0;
        std::shared_ptr<const std::vector<std::pair<std::wstring, bool>>> targets;
    };
    struct Resolutions {
        std::uint64_t scan = 0;
        std::wstring root;
        FileStamp schemaStamp;
        std::map<std::wstring, Resolved> modules;
    };
    static std::mutex resolvedLock;
    static Resolutions resolved;
    const std::uint64_t scan = imageScanGeneration();
    std::unique_lock resolvedGuard(resolvedLock, std::defer_lock);
    if (scan != 0) {
        resolvedGuard.lock();
        const FileStamp schemaStamp = fileStamp(mountDir / L"Windows" / L"System32" / L"apisetschema.dll");
        if (const std::wstring root = lower(mountDir.wstring());
            resolved.scan != scan || resolved.root != root || resolved.schemaStamp != schemaStamp) {
            resolved = Resolutions{scan, root, schemaStamp, {}};
        }
    }
    // A file about to go → who imports it (and whether only on demand).
    std::map<std::wstring, std::vector<std::pair<std::wstring, bool>>> importers;
    for (const auto& module : modules) {
        std::shared_ptr<const std::vector<std::pair<std::wstring, bool>>> targets;
        if (scan != 0) {
            auto& entry = resolved.modules[module.path];
            if (!entry.targets || entry.size != module.size || entry.written != module.written) {
                entry = Resolved{module.size, module.written, resolve(module)};
            }
            targets = entry.targets;
        } else {
            targets = resolve(module);
        }
        for (const auto& [candidate, delayed] : *targets) {
            if (goingSet.contains(candidate)) {
                importers[candidate].emplace_back(module.path, delayed);
            }
        }
    }
    // An importer counts when it stays: it is not going, or it is kept itself (until nothing changes).
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [target, list] : importers) {
            if (kept.contains(target)) {
                continue;
            }
            for (const auto& [importer, delayed] : list) {
                if (goingSet.contains(importer) && !kept.contains(importer)) {
                    continue;
                }
                const std::wstring name = importer.substr(importer.rfind(L'\\') + 1);
                kept.emplace(target, name + (delayed ? L" delay-loads it" : L" imports it"));
                changed = true;
                break;
            }
        }
    }
    return kept;
}

} // namespace wl::core
