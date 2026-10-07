#include "core/image/wim/WimVerify.h"

#include "base/Log.h"
#include "core/image/WimFile.h"
#include "core/image/wim/Lzx.h"

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <thread>

namespace wl::core {

namespace {

constexpr std::size_t kEntrySize = 50; // lookup table entry
constexpr std::uint8_t kFree = 0x01;
constexpr std::uint8_t kMetadata = 0x02;
constexpr std::uint8_t kCompressed = 0x04;
constexpr std::uint8_t kSolid = 0x10;
constexpr std::size_t kReadAhead = 4u << 20; // compressed bytes read per request

struct Entry {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;     // as stored
    std::uint64_t original = 0; // uncompressed
    std::uint8_t flags = 0;
    std::uint16_t part = 1;
    std::uint32_t refCount = 0;
    std::array<std::uint8_t, 20> hash{};
};

template <class T>
T le(const std::byte* p) {
    T value{};
    std::memcpy(&value, p, sizeof(T));
    return value;
}

// XPRESS (LZ77 + Huffman) is in ntdll.
using DecompressFn = LONG(NTAPI*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);
using WorkSpaceFn = LONG(NTAPI*)(USHORT, PULONG, PULONG);
constexpr USHORT kXpressHuff = 0x0004; // COMPRESSION_FORMAT_XPRESS_HUFF

struct Xpress {
    DecompressFn decompress = nullptr;
    ULONG workspace = 0;
};
const Xpress& xpress() {
    static const Xpress instance = [] {
        Xpress x;
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto size = reinterpret_cast<WorkSpaceFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize")));
        const auto run =
            reinterpret_cast<DecompressFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlDecompressBufferEx")));
        ULONG compress = 0;
        ULONG fragment = 0;
        if (size && run && size(kXpressHuff, &compress, &fragment) >= 0) {
            x.decompress = run;
            x.workspace = std::max(compress, fragment);
        }
        return x;
    }();
    return instance;
}

// One SHA-1 object per worker, reused for every stream.
class Sha1 {
public:
    Sha1() {
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&m_algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0))) {
            if (!BCRYPT_SUCCESS(BCryptCreateHash(m_algorithm, &m_hash, nullptr, 0, nullptr, 0, BCRYPT_HASH_REUSABLE_FLAG))) {
                m_hash = nullptr;
            }
        }
    }
    ~Sha1() {
        if (m_hash) {
            BCryptDestroyHash(m_hash);
        }
        if (m_algorithm) {
            BCryptCloseAlgorithmProvider(m_algorithm, 0);
        }
    }
    Sha1(const Sha1&) = delete;
    Sha1& operator=(const Sha1&) = delete;

    [[nodiscard]] bool usable() const noexcept { return m_hash != nullptr; }
    void update(std::span<const std::byte> data) noexcept {
        BCryptHashData(m_hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),
                       static_cast<ULONG>(data.size()), 0);
    }
    // Also readies the object for the next stream.
    [[nodiscard]] std::array<std::uint8_t, 20> finish() noexcept {
        std::array<std::uint8_t, 20> digest{};
        BCryptFinishHash(m_hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        return digest;
    }

private:
    BCRYPT_ALG_HANDLE m_algorithm = nullptr;
    BCRYPT_HASH_HANDLE m_hash = nullptr;
};

struct Worker {
    const ByteSource& source;
    WimCompression compression;
    std::uint32_t chunkSize;
    Sha1 sha;
    std::vector<std::byte> stored;   // compressed bytes of a run of chunks
    std::vector<std::byte> plain;    // one chunk, uncompressed
    std::vector<std::byte> table;    // chunk table of the stream
    std::vector<std::byte> workspace;
    std::vector<std::byte>* sink = nullptr; // read(): the bytes are kept instead of hashed

    void consume(std::span<const std::byte> data) {
        if (sink) {
            sink->insert(sink->end(), data.begin(), data.end());
        } else {
            sha.update(data);
        }
    }

    Worker(const ByteSource& s, WimCompression c, std::uint32_t chunk)
        : source(s), compression(c), chunkSize(chunk), plain(chunk), workspace(xpress().workspace) {}

    [[nodiscard]] bool decompress(std::span<const std::byte> in, std::span<std::byte> out) {
        if (compression == WimCompression::Lzx) {
            return lzxDecompressChunk(in, out);
        }
        ULONG written = 0;
        const LONG status = xpress().decompress(kXpressHuff, reinterpret_cast<PUCHAR>(out.data()),
                                                static_cast<ULONG>(out.size()),
                                                reinterpret_cast<PUCHAR>(const_cast<std::byte*>(in.data())),
                                                static_cast<ULONG>(in.size()), &written, workspace.data());
        return status >= 0 && written == out.size();
    }

    // Empty: the stream is what its hash says. Otherwise what is wrong with it.
    [[nodiscard]] std::wstring check(const Entry& entry, const std::atomic<bool>& stop) {
        if (entry.offset > source.size() || entry.size > source.size() - entry.offset) {
            return L"lies outside the file (truncated?)";
        }
        if ((entry.flags & kCompressed) == 0) {
            if (entry.size != entry.original) {
                return L"stored size does not match its size";
            }
            stored.resize(static_cast<std::size_t>(std::min<std::uint64_t>(entry.size, kReadAhead)));
            for (std::uint64_t done = 0; done < entry.size && !stop;) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(entry.size - done, stored.size()));
                if (!source.read(entry.offset + done, std::span(stored.data(), count))) {
                    (void)sha.finish();
                    return L"cannot be read";
                }
                consume(std::span(stored.data(), count));
                done += count;
            }
        } else if (auto problem = hashChunks(entry, stop); !problem.empty()) {
            (void)sha.finish();
            return problem;
        }
        const auto digest = sha.finish();
        static constexpr std::array<std::uint8_t, 20> kNoHash{};
        if (stop || entry.hash == kNoHash) {
            return {}; // stopped by the caller, or nothing to compare against
        }
        return digest == entry.hash ? std::wstring() : std::wstring(L"content does not match its SHA-1");
    }

    // The whole stream, uncompressed, into `out`. Empty: read; otherwise what is wrong with it.
    [[nodiscard]] std::wstring read(const Entry& entry, std::vector<std::byte>& out) {
        out.clear();
        if (entry.offset > source.size() || entry.size > source.size() - entry.offset) {
            return L"lies outside the file (truncated?)";
        }
        if ((entry.flags & kCompressed) == 0) {
            out.resize(static_cast<std::size_t>(entry.size));
            return out.empty() || source.read(entry.offset, out) ? std::wstring() : std::wstring(L"cannot be read");
        }
        out.reserve(static_cast<std::size_t>(entry.original));
        const std::atomic<bool> stop{false};
        sink = &out;
        std::wstring problem = hashChunks(entry, stop);
        sink = nullptr;
        return problem;
    }

    [[nodiscard]] std::wstring hashChunks(const Entry& entry, const std::atomic<bool>& stop) {
        const std::uint64_t chunks = (entry.original + chunkSize - 1) / chunkSize;
        if (chunks == 0) {
            return {};
        }
        const std::size_t width = entry.original > 0xFFFFFFFFull ? 8 : 4;
        const std::uint64_t tableBytes = (chunks - 1) * width;
        if (tableBytes > entry.size) {
            return L"chunk table larger than the stream";
        }
        table.resize(static_cast<std::size_t>(tableBytes));
        if (!table.empty() && !source.read(entry.offset, table)) {
            return L"cannot be read";
        }
        const std::uint64_t dataSize = entry.size - tableBytes;
        // Where chunk `i` starts within the chunk data (the table has no entry for chunk 0).
        auto startOf = [&](std::uint64_t i) -> std::uint64_t {
            if (i == 0) {
                return 0;
            }
            if (i >= chunks) {
                return dataSize;
            }
            const std::byte* at = table.data() + (i - 1) * width;
            return width == 8 ? le<std::uint64_t>(at) : le<std::uint32_t>(at);
        };
        const std::uint64_t dataOffset = entry.offset + tableBytes;
        for (std::uint64_t i = 0; i < chunks && !stop;) {
            // A run of whole chunks of up to kReadAhead bytes, at least one.
            const std::uint64_t runStart = startOf(i);
            std::uint64_t end = i + 1;
            while (end < chunks && startOf(end + 1) >= runStart && startOf(end + 1) - runStart <= kReadAhead) {
                ++end;
            }
            const std::uint64_t runEnd = startOf(end);
            if (runEnd < runStart || runEnd > dataSize || runEnd - runStart > (kReadAhead + 2 * std::uint64_t{chunkSize})) {
                return L"chunk table is not in order";
            }
            stored.resize(static_cast<std::size_t>(runEnd - runStart));
            if (!stored.empty() && !source.read(dataOffset + runStart, stored)) {
                return L"cannot be read";
            }
            for (; i < end; ++i) {
                const std::uint64_t from = startOf(i);
                const std::uint64_t to = startOf(i + 1);
                const auto plainSize = static_cast<std::size_t>(
                    std::min<std::uint64_t>(chunkSize, entry.original - i * chunkSize));
                if (to < from || to > runEnd || to - from > plainSize || to == from) {
                    return L"chunk table is not in order";
                }
                const std::span<const std::byte> in(stored.data() + (from - runStart), static_cast<std::size_t>(to - from));
                if (in.size() == plainSize) {
                    consume(in); // stored as it is: compression did not help
                    continue;
                }
                const std::span<std::byte> out(plain.data(), plainSize);
                if (!decompress(in, out)) {
                    return std::format(L"chunk {} does not decompress", i);
                }
                consume(out);
            }
        }
        return {};
    }
};

struct Table {
    WimHeader header;
    std::uint32_t chunkSize = 0;
    std::vector<Entry> entries; // by offset
    std::uint64_t total = 0;    // stored bytes
};

Result<Table> readTable(const ByteSource& wim) {
    auto header = readWimHeader(wim);
    if (!header) {
        return std::unexpected(header.error());
    }
    if (header->solid || header->compression == WimCompression::Lzms || header->compression == WimCompression::Unknown) {
        return fail(ErrorCode::Unsupported, L"an ESD (LZMS) image cannot be verified; convert it to WIM first", L"WIM");
    }
    if (header->compression == WimCompression::Xpress && !xpress().decompress) {
        return fail(ErrorCode::Unsupported, L"XPRESS decompression is not available on this system", L"WIM");
    }
    if (header->lookupCompressed || header->lookupSize % kEntrySize != 0 || header->lookupOffset > wim.size() ||
        header->lookupSize > wim.size() - header->lookupOffset || header->lookupSize > (1ull << 31)) {
        return fail(ErrorCode::ParseError, L"the stream table of the image cannot be read", L"WIM");
    }
    const std::uint32_t chunkSize = header->compression == WimCompression::None ? 32768 : header->chunkSize;
    if (chunkSize != 32768) {
        return fail(ErrorCode::Unsupported, L"chunk sizes other than 32 KiB are not supported", std::to_wstring(chunkSize));
    }

    std::vector<std::byte> raw(static_cast<std::size_t>(header->lookupSize));
    if (!raw.empty()) {
        if (auto read = wim.read(header->lookupOffset, raw); !read) {
            return std::unexpected(read.error());
        }
    }
    std::vector<Entry> entries;
    entries.reserve(raw.size() / kEntrySize);
    std::uint64_t total = 0;
    for (std::size_t at = 0; at + kEntrySize <= raw.size(); at += kEntrySize) {
        const std::byte* p = raw.data() + at;
        const auto packed = le<std::uint64_t>(p);
        Entry entry;
        entry.size = packed & 0x00FFFFFFFFFFFFFFull;
        entry.flags = static_cast<std::uint8_t>(packed >> 56);
        entry.offset = le<std::uint64_t>(p + 8);
        entry.original = le<std::uint64_t>(p + 16);
        const auto part = le<std::uint16_t>(p + 24);
        entry.part = part;
        entry.refCount = le<std::uint32_t>(p + 26);
        std::memcpy(entry.hash.data(), p + 30, entry.hash.size());
        if (entry.flags & kSolid) {
            return fail(ErrorCode::Unsupported, L"an image with solid resources (ESD) cannot be verified", L"WIM");
        }
        if ((entry.flags & kFree) || part != header->partNumber) {
            continue; // unused, or in another part of a split image
        }
        total += entry.size;
        entries.push_back(entry);
    }
    return Table{*header, chunkSize, std::move(entries), total}; // lookup order: callers sort a copy to read in sequence
}

// An edition's metadata resource: the security data (its length first, 8-aligned), then the root
// directory entry. A directory entry (wimlib's wim_dentry_on_disk) is 102 bytes before its UTF-16
// name; its children are a run of entries from `subdir`, ended by a length of 0 (or under 8); each
// entry is followed by its extra stream entries, every one 8-aligned.
constexpr std::size_t kDentryName = 102;
constexpr std::uint32_t kDirectory = 0x10; // FILE_ATTRIBUTE_DIRECTORY

std::uint64_t align8(std::uint64_t v) {
    return (v + 7) & ~std::uint64_t{7};
}

struct Dentry {
    std::uint64_t next = 0; // the sibling after it (streams skipped)
    std::uint64_t subdir = 0;
    std::uint32_t attributes = 0;
    std::wstring_view name;
};

// nullopt: the end of the directory, or an entry that does not fit (a damaged list ends there).
std::optional<Dentry> dentryAt(std::span<const std::byte> meta, std::uint64_t at) {
    if (at > meta.size() || meta.size() - at < 8) {
        return std::nullopt;
    }
    const std::uint64_t length = le<std::uint64_t>(meta.data() + at);
    if (length < kDentryName || length > meta.size() - at) {
        return std::nullopt;
    }
    const std::byte* p = meta.data() + at;
    Dentry d;
    d.attributes = le<std::uint32_t>(p + 8);
    d.subdir = le<std::uint64_t>(p + 16);
    const auto streams = le<std::uint16_t>(p + 96);
    const auto nameBytes = le<std::uint16_t>(p + 100);
    if (kDentryName + nameBytes > length) {
        return std::nullopt;
    }
    d.name = std::wstring_view(reinterpret_cast<const wchar_t*>(p + kDentryName), nameBytes / sizeof(wchar_t));
    std::uint64_t next = at + align8(length);
    for (std::uint16_t i = 0; i < streams; ++i) {
        if (next > meta.size() || meta.size() - next < 8) {
            return std::nullopt;
        }
        const std::uint64_t streamLength = le<std::uint64_t>(meta.data() + next);
        if (streamLength < 8) {
            return std::nullopt;
        }
        next += align8(streamLength);
    }
    d.next = next;
    return d;
}

bool sameName(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

// The entry `path` names ("Windows\System32", either slash, any case; "" = the root folder).
std::optional<Dentry> entryInMetadata(std::span<const std::byte> meta, std::wstring_view path) {
    if (meta.size() < 8) {
        return std::nullopt;
    }
    const std::uint32_t security = le<std::uint32_t>(meta.data());
    std::optional<Dentry> current = dentryAt(meta, security == 0 ? 8 : align8(security));
    std::size_t start = 0;
    while (current && start <= path.size()) {
        std::size_t end = path.find_first_of(L"\\/", start);
        if (end == std::wstring_view::npos) {
            end = path.size();
        }
        const std::wstring_view part = path.substr(start, end - start);
        start = end + 1;
        if (part.empty()) {
            continue; // "\Windows" or a doubled slash
        }
        if ((current->attributes & kDirectory) == 0) {
            return std::nullopt; // a file in the middle of the path
        }
        std::optional<Dentry> found;
        // Bounded: a damaged list could point back into itself.
        for (std::uint64_t at = current->subdir, guard = 0; at != 0 && guard < 1'000'000; ++guard) {
            const auto d = dentryAt(meta, at);
            if (!d || d->next <= at) {
                break;
            }
            if (sameName(d->name, part)) {
                found = d;
                break;
            }
            at = d->next;
        }
        current = found;
    }
    return current;
}

bool fileInMetadata(std::span<const std::byte> meta, std::wstring_view path) {
    const auto entry = entryInMetadata(meta, path);
    return entry && (entry->attributes & kDirectory) == 0;
}

std::vector<std::wstring> namesInMetadata(std::span<const std::byte> meta, std::wstring_view folder) {
    std::vector<std::wstring> names;
    const auto entry = entryInMetadata(meta, folder);
    if (!entry || (entry->attributes & kDirectory) == 0) {
        return names;
    }
    for (std::uint64_t at = entry->subdir, guard = 0; at != 0 && guard < 1'000'000; ++guard) {
        const auto d = dentryAt(meta, at);
        if (!d || d->next <= at) {
            break;
        }
        names.emplace_back(d->name);
        at = d->next;
    }
    return names;
}

} // namespace

namespace {

// One edition's file list (its metadata resource), decompressed.
Result<std::vector<std::byte>> editionMetadata(const ByteSource& wim, int index) {
    auto table = readTable(wim);
    if (!table) {
        return std::unexpected(table.error());
    }
    // Editions are the metadata resources in the order the lookup table lists them.
    const Entry* metadata = nullptr;
    int seen = 0;
    for (const auto& entry : table->entries) {
        if ((entry.flags & kMetadata) && ++seen == index) {
            metadata = &entry;
            break;
        }
    }
    if (!metadata) {
        return fail(ErrorCode::NotFound, std::format(L"no file list for edition {} in this file", index), L"WIM");
    }
    Worker worker(wim, table->header.compression, table->chunkSize);
    std::vector<std::byte> meta;
    if (auto problem = worker.read(*metadata, meta); !problem.empty()) {
        return fail(ErrorCode::ParseError, L"the file list of the edition " + problem, std::to_wstring(index));
    }
    return meta;
}

} // namespace

Result<bool> wimFileExists(const ByteSource& wim, int index, std::wstring_view path) {
    auto meta = editionMetadata(wim, index);
    if (!meta) {
        return std::unexpected(meta.error());
    }
    return fileInMetadata(*meta, path);
}

Result<std::vector<std::wstring>> wimFolderNames(const ByteSource& wim, int index, std::wstring_view folder) {
    auto meta = editionMetadata(wim, index);
    if (!meta) {
        return std::unexpected(meta.error());
    }
    return namesInMetadata(*meta, folder);
}

Result<WimVerifyReport> verifyWim(const ByteSource& wim, const TaskContext& task) {
    auto table = readTable(wim);
    if (!table) {
        return std::unexpected(table.error());
    }
    const auto& header = table->header;
    const std::uint32_t chunkSize = table->chunkSize;
    std::vector<Entry> entries = table->entries;
    std::ranges::sort(entries, {}, &Entry::offset); // sequential reads
    const std::uint64_t total = table->total;

    WimVerifyReport report;
    std::mutex mutex; // report + wake-up
    std::condition_variable wake;
    std::atomic<std::size_t> next{0};
    std::atomic<std::uint64_t> doneBytes{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> hashFailed{false};
    unsigned running = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    running = static_cast<unsigned>(std::min<std::size_t>(running, std::max<std::size_t>(entries.size(), 1)));

    auto work = [&] {
        Worker worker(wim, header.compression, chunkSize);
        if (!worker.sha.usable()) {
            hashFailed = true;
            stop = true;
        }
        for (std::size_t i = next++; i < entries.size() && !stop; i = next++) {
            const Entry& entry = entries[i];
            std::wstring problem = worker.check(entry, stop);
            doneBytes += entry.size;
            if (stop) {
                break;
            }
            std::scoped_lock lock(mutex);
            ++report.streams;
            report.bytes += entry.original;
            if (!problem.empty()) {
                ++report.damaged;
                report.first.push_back({entry.offset, entry.size, (entry.flags & kMetadata) != 0, std::move(problem)});
            }
        }
        std::scoped_lock lock(mutex);
        --running;
        wake.notify_all();
    };
    std::vector<std::thread> threads;
    const unsigned count = running;
    for (unsigned i = 0; i < count; ++i) {
        threads.emplace_back(work);
    }
    {
        // Progress and cancellation from this thread only: the callback need not be thread-safe.
        std::unique_lock lock(mutex);
        while (running > 0) {
            wake.wait_for(lock, std::chrono::milliseconds(100));
            if (task.cancel.cancelled()) {
                stop = true;
            }
            lock.unlock();
            task.report(total ? static_cast<double>(doneBytes.load()) / static_cast<double>(total) : 1.0, L"verify");
            lock.lock();
        }
    }
    for (auto& thread : threads) {
        thread.join();
    }
    if (task.cancel.cancelled()) {
        return fail(ErrorCode::Cancelled, L"verification cancelled", L"WIM");
    }
    if (hashFailed) {
        return fail(ErrorCode::Unknown, L"SHA-1 is not available", L"WIM");
    }
    std::ranges::sort(report.first, {}, &WimVerifyReport::Damage::offset);
    if (report.first.size() > WimVerifyReport::kMaxListed) {
        report.first.resize(WimVerifyReport::kMaxListed);
    }
    for (const auto& damage : report.first) {
        log::warn("wim", std::format(L"damaged stream at {} ({} bytes{}): {}", damage.offset, damage.size,
                                     damage.metadata ? L", metadata" : L"", damage.reason));
    }
    log::info("wim", std::format(L"verified {} streams, {} bytes: {} damaged", report.streams, report.bytes, report.damaged));
    task.report(1.0, L"verify");
    return report;
}

Result<std::vector<SolidResource>> solidResources(const ByteSource& wim) {
    auto header = readWimHeader(wim);
    if (!header) {
        return std::unexpected(header.error());
    }
    if (header->lookupCompressed || header->lookupSize % kEntrySize != 0 || header->lookupOffset > wim.size() ||
        header->lookupSize > wim.size() - header->lookupOffset || header->lookupSize > (1ull << 31)) {
        return fail(ErrorCode::ParseError, L"the stream table of the image cannot be read", L"WIM");
    }
    std::vector<std::byte> raw(static_cast<std::size_t>(header->lookupSize));
    if (!raw.empty()) {
        if (auto read = wim.read(header->lookupOffset, raw); !read) {
            return std::unexpected(read.error());
        }
    }
    // A solid resource's own entry has this "uncompressed size"; the streams in it point into it.
    constexpr std::uint64_t kSolidMagic = 0x100000000ull;
    std::vector<SolidResource> out;
    for (std::size_t at = 0; at + kEntrySize <= raw.size(); at += kEntrySize) {
        const std::byte* p = raw.data() + at;
        const auto packed = le<std::uint64_t>(p);
        const auto flags = static_cast<std::uint8_t>(packed >> 56);
        const auto part = le<std::uint16_t>(p + 24);
        if (!(flags & kSolid) || (flags & kFree) || le<std::uint64_t>(p + 16) != kSolidMagic || part != header->partNumber) {
            continue;
        }
        SolidResource resource;
        resource.size = packed & 0x00FFFFFFFFFFFFFFull;
        resource.offset = le<std::uint64_t>(p + 8);
        // Its header: uncompressed size (8), chunk size (4), compression (4: 1 XPRESS, 2 LZX, 3 LZMS).
        std::array<std::byte, 16> head{};
        if (resource.offset > wim.size() || wim.size() - resource.offset < head.size()) {
            return fail(ErrorCode::ParseError, L"a solid resource lies past the end of the file", L"WIM");
        }
        if (auto read = wim.read(resource.offset, head); !read) {
            return std::unexpected(read.error());
        }
        resource.chunkSize = le<std::uint32_t>(head.data() + 8);
        switch (le<std::uint32_t>(head.data() + 12)) {
        case 0: resource.compression = WimCompression::None; break;
        case 1: resource.compression = WimCompression::Xpress; break;
        case 2: resource.compression = WimCompression::Lzx; break;
        case 3: resource.compression = WimCompression::Lzms; break;
        default: resource.compression = WimCompression::Unknown; break;
        }
        out.push_back(resource);
    }
    return out;
}

} // namespace wl::core
