#include "core/image/icons/ResFile.h"

#include "base/File.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/icons/IconGroups.h"
#include "core/system/Process.h"

#include <json.hpp>

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace wl::core {

namespace {

constexpr std::size_t kMaxRes = 64u * 1024 * 1024;

Error bad(std::wstring what) {
    return Error{ErrorCode::ParseError, std::move(what), L".res"};
}

template <class T>
bool readAt(std::string_view bytes, std::size_t offset, T& out) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return false;
    }
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return true;
}

// A .res name or type: 0xFFFF + id, or a zero-terminated UTF-16 string.
bool readKey(std::string_view bytes, std::size_t& offset, std::size_t end, ResourceKey& key) {
    std::uint16_t first = 0;
    if (offset + 2 > end || !readAt(bytes, offset, first)) {
        return false;
    }
    if (first == 0xFFFF) {
        std::uint16_t id = 0;
        if (offset + 4 > end || !readAt(bytes, offset + 2, id)) {
            return false;
        }
        key = ResourceKey{id, {}};
        offset += 4;
        return true;
    }
    std::wstring name;
    for (;;) {
        std::uint16_t ch = 0;
        if (offset + 2 > end || !readAt(bytes, offset, ch)) {
            return false;
        }
        offset += 2;
        if (ch == 0) {
            break;
        }
        name.push_back(static_cast<wchar_t>(ch));
        if (name.size() > 256) {
            return false;
        }
    }
    if (name.empty()) {
        return false;
    }
    key = ResourceKey{0, std::move(name)};
    return true;
}

template <class Vec, class Make>
auto& findOrAdd(Vec& list, const ResourceKey& key, Make make) {
    const auto it = std::ranges::find_if(list, [&](const auto& e) { return e.key == key; });
    if (it != list.end()) {
        return *it;
    }
    list.push_back(make());
    return list.back();
}

std::wstring trim(std::wstring s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return {};
    }
    s.erase(0, first);
    s.erase(s.find_last_not_of(L" \t\r\n") + 1);
    return s;
}

bool hasResFiles(const std::filesystem::path& folder) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (e.is_regular_file(ec) && text::lower(e.path().extension().wstring()) == L".res") {
            return true;
        }
    }
    return false;
}

// The folder holding Pack.ini or Resources\*.res: `folder` itself or one of its subfolders.
std::optional<std::filesystem::path> packRoot(const std::filesystem::path& folder) {
    std::error_code ec;
    auto looksLikePack = [&](const std::filesystem::path& dir) {
        return std::filesystem::is_regular_file(dir / L"Pack.ini", ec) ||
               (std::filesystem::is_directory(dir / L"Resources", ec) && hasResFiles(dir / L"Resources"));
    };
    if (looksLikePack(folder)) {
        return folder;
    }
    for (const auto& e : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (e.is_directory(ec) && !e.is_symlink(ec) && looksLikePack(e.path())) {
            return e.path();
        }
    }
    return std::nullopt;
}

} // namespace

Result<ResourceTree> parseResFile(std::string_view bytes) {
    if (bytes.size() > kMaxRes) {
        return std::unexpected(bad(L"too large"));
    }
    ResourceTree tree;
    std::size_t offset = 0;
    while (offset + 8 <= bytes.size()) {
        std::uint32_t dataSize = 0;
        std::uint32_t headerSize = 0;
        readAt(bytes, offset, dataSize);
        readAt(bytes, offset + 4, headerSize);
        if (headerSize < 32 || headerSize > bytes.size() - offset || dataSize > bytes.size() - offset - headerSize) {
            return std::unexpected(bad(L"entry out of bounds"));
        }
        const std::size_t headerEnd = offset + headerSize;
        std::size_t p = offset + 8;
        ResourceKey type;
        ResourceKey name;
        if (!readKey(bytes, p, headerEnd, type) || !readKey(bytes, p, headerEnd, name)) {
            return std::unexpected(bad(L"bad type or name"));
        }
        p = (p + 3) & ~std::size_t{3};
        // DataVersion (4), MemoryFlags (2), LanguageId (2), Version (4), Characteristics (4).
        std::uint16_t language = 0;
        if (p + 16 > headerEnd || !readAt(bytes, p + 6, language)) {
            return std::unexpected(bad(L"short header"));
        }
        // The first entry rc.exe writes is empty (type 0): it only marks the file as 32-bit .res.
        if (dataSize > 0 && !(type == ResourceKey{0, {}})) {
            auto& t = findOrAdd(tree.types, type, [&] { return ResourceType{type, {}, {}}; });
            auto& n = findOrAdd(t.names, name, [&] { return ResourceName{name, {}, {}}; });
            n.languages.push_back(ResourceLanguage{language, 0, std::string(bytes.substr(headerEnd, dataSize))});
        }
        offset = (headerEnd + dataSize + 3) & ~std::size_t{3};
    }
    if (offset < bytes.size() && bytes.size() - offset >= 8) {
        return std::unexpected(bad(L"trailing bytes"));
    }
    return tree;
}

std::string writeResFile(const ResourceTree& tree) {
    std::string out;
    auto put16 = [](std::string& s, std::uint16_t v) { s.append(reinterpret_cast<const char*>(&v), 2); };
    auto put32 = [](std::string& s, std::uint32_t v) { s.append(reinterpret_cast<const char*>(&v), 4); };
    auto putKey = [&](std::string& s, const ResourceKey& key) {
        if (key.named()) {
            for (const wchar_t c : key.name) {
                put16(s, static_cast<std::uint16_t>(c));
            }
            put16(s, 0);
        } else {
            put16(s, 0xFFFF);
            put16(s, key.id);
        }
    };
    auto entry = [&](const ResourceKey& type, const ResourceKey& name, std::uint16_t language, std::string_view data) {
        std::string header;
        putKey(header, type);
        putKey(header, name);
        while ((header.size() + 8) % 4 != 0) {
            header.push_back('\0');
        }
        put32(header, 0);      // DataVersion
        put16(header, 0x1030); // MemoryFlags: MOVEABLE | PURE | DISCARDABLE
        put16(header, language);
        put32(header, 0);      // Version
        put32(header, 0);      // Characteristics
        put32(out, static_cast<std::uint32_t>(data.size()));
        put32(out, static_cast<std::uint32_t>(header.size() + 8));
        out += header;
        out.append(data);
        while (out.size() % 4 != 0) {
            out.push_back('\0');
        }
    };
    entry(ResourceKey{0, {}}, ResourceKey{0, {}}, 0, {});
    for (const auto& type : tree.types) {
        for (const auto& name : type.names) {
            for (const auto& language : name.languages) {
                entry(type.key, name.key, language.language, language.data);
            }
        }
    }
    return out;
}

bool is7tspPack(const std::filesystem::path& folder) {
    std::error_code ec;
    return std::filesystem::is_directory(folder, ec) && packRoot(folder).has_value();
}

Result<SevenTspPack> read7tspPack(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return fail(ErrorCode::NotFound, L"not a folder", folder.wstring());
    }
    const auto root = packRoot(folder);
    if (!root) {
        return fail(ErrorCode::NotFound, L"not a 7TSP pack (no Pack.ini or Resources\\*.res)", folder.wstring());
    }
    SevenTspPack pack;
    pack.name = root->filename().wstring();
    if (auto ini = readFileBytes(*root / L"Pack.ini"); ini && ini->size() < 64 * 1024) {
        std::wstring text = utf8::decodeText(*ini);
        std::size_t start = 0;
        while (start < text.size()) {
            auto end = text.find(L'\n', start);
            if (end == std::wstring::npos) {
                end = text.size();
            }
            const std::wstring line = trim(text.substr(start, end - start));
            start = end + 1;
            const auto eq = line.find(L'=');
            if (eq == std::wstring::npos) {
                continue;
            }
            const std::wstring key = text::lower(trim(line.substr(0, eq)));
            const std::wstring value = trim(line.substr(eq + 1));
            if (value.empty()) {
                continue;
            }
            if (key == L"pack") {
                pack.name = value;
            } else if ((key == L"base by" || key == L"theme creator") && pack.author.empty()) {
                pack.author = value;
            }
        }
    }
    const auto resources = std::filesystem::is_directory(*root / L"Resources", ec) ? *root / L"Resources" : *root;
    for (const auto& e : std::filesystem::directory_iterator(resources, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!e.is_regular_file(ec) || text::lower(e.path().extension().wstring()) != L".res") {
            continue;
        }
        pack.files.push_back({e.path().stem().wstring(), e.path()});
    }
    std::ranges::sort(pack.files, [](const auto& a, const auto& b) { return text::lower(a.target) < text::lower(b.target); });
    if (pack.files.empty()) {
        return fail(ErrorCode::NotFound, L"the 7TSP pack has no .res files", root->wstring());
    }
    return pack;
}

std::wstring packFileStem(const ResourceKey& key) {
    if (!key.named()) {
        return std::to_wstring(key.id);
    }
    const bool safe = key.name.size() <= 120 && std::ranges::all_of(key.name, [](wchar_t c) {
                          return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') ||
                                 c == L'_' || c == L'-' || c == L'.' || c == L' ';
                      });
    // A name of digits only would read back as an id.
    const bool digits = std::ranges::all_of(key.name, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    return safe && !digits && key.name.back() != L'.' && key.name.back() != L' ' ? key.name : std::wstring();
}

Result<int> convert7tspPack(const SevenTspPack& pack, const std::filesystem::path& out, std::vector<std::wstring>* skipped) {
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"could not create the folder", out.wstring(), ec.value());
    }
    auto skip = [&](const std::wstring& target, const std::wstring& why) {
        if (skipped) {
            skipped->push_back(target + L": " + why);
        }
    };
    int written = 0;
    for (const auto& file : pack.files) {
        auto bytes = readFileBytes(file.res);
        if (!bytes) {
            skip(file.target, bytes.error().message);
            continue;
        }
        auto tree = parseResFile(*bytes);
        if (!tree) {
            skip(file.target, tree.error().message);
            continue;
        }
        const auto groups = listIconGroups(*tree);
        if (groups.empty()) {
            skip(file.target, L"no icons");
            continue;
        }
        // Folder name = the target ("imageres.dll.mun"); a pack path never leaves `out`.
        if (file.target.empty() || file.target.find_first_of(L"\\/:") != std::wstring::npos || file.target.find(L"..") != std::wstring::npos) {
            skip(file.target, L"bad file name");
            continue;
        }
        const auto dir = out / file.target;
        std::filesystem::create_directories(dir, ec);
        for (const auto& group : groups) {
            const std::wstring stem = packFileStem(group.key);
            if (group.images.empty() || stem.empty()) {
                skip(file.target, group.key.text());
                continue;
            }
            if (auto ok = writeFileAtomic(dir / (stem + L".ico"), makeIco(group.images)); !ok) {
                return std::unexpected(ok.error());
            }
            ++written;
        }
    }
    nlohmann::json manifest{{"name", utf8::fromWide(pack.name)}, {"author", utf8::fromWide(pack.author)}, {"format", "7tsp"}};
    if (auto ok = writeFileAtomic(out / L"iconpack.json", manifest.dump(2)); !ok) {
        return std::unexpected(ok.error());
    }
    return written;
}

Result<void> extractArchive(const std::filesystem::path& archive, const std::filesystem::path& out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(archive, ec)) {
        return fail(ErrorCode::NotFound, L"no such file", archive.wstring());
    }
    std::filesystem::create_directories(out, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"could not create the folder", out.wstring(), ec.value());
    }
    auto run = [&](const std::wstring& commandLine) -> std::optional<std::string> {
        std::string output;
        auto code = runProcess(commandLine, [&](std::string_view s) {
            if (output.size() < 8192) {
                output.append(s);
            }
        });
        if (code && *code == 0) {
            return std::nullopt;
        }
        return code ? output : std::string("could not start");
    };
    std::wstring failure;
    if (auto tar = systemTool(L"tar.exe")) {
        // bsdtar refuses absolute paths and ".." without -P.
        const auto error = run(L"\"" + *tar + L"\" -x -f \"" + archive.wstring() + L"\" -C \"" + out.wstring() + L"\"");
        if (!error) {
            return {};
        }
        failure = utf8::decodeText(*error);
    }
    // 7-Zip (x keeps folders; -y answers its questions; it refuses "..": "Dangerous link path").
    wchar_t programFiles[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"ProgramW6432", programFiles, MAX_PATH);
    const std::filesystem::path sevenZip =
        std::filesystem::path(n > 0 && n < MAX_PATH ? std::wstring(programFiles, n) : L"C:\\Program Files") / L"7-Zip" / L"7z.exe";
    if (std::filesystem::is_regular_file(sevenZip, ec)) {
        std::filesystem::remove_all(out, ec); // what tar may have left half-written
        std::filesystem::create_directories(out, ec);
        const auto error = run(L"\"" + sevenZip.wstring() + L"\" x -y \"-o" + out.wstring() + L"\" \"" + archive.wstring() + L"\"");
        if (!error) {
            return {};
        }
        failure = utf8::decodeText(*error);
    }
    return fail(ErrorCode::IoError, L"could not open the archive (extract it and load the folder)", trim(failure));
}

} // namespace wl::core
