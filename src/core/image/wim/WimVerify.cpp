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
    std::function<bool(std::span<const std::byte>)> sink; // optional: the plain bytes, in order
    bool sinkFailed = false;
    std::uint64_t limit = UINT64_MAX; // WimStreamReader::read maxBytes
    std::uint64_t produced = 0;
    [[nodiscard]] bool full() const noexcept { return produced >= limit; }

    void consume(std::span<const std::byte> data) noexcept {
        if (full()) {
            return;
        }
        if (data.size() > limit - produced) {
            data = data.first(static_cast<std::size_t>(limit - produced));
        }
        produced += data.size();
        sha.update(data);
        if (sink && !sinkFailed && !sink(data)) {
            sinkFailed = true;
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
            for (std::uint64_t done = 0; done < entry.size && !stop && !full() && !sinkFailed;) {
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
        if (stop || entry.hash == kNoHash || produced < entry.original) {
            return {}; // cut short on purpose (limit) or by the caller: nothing to compare
        }
        return digest == entry.hash ? std::wstring() : std::wstring(L"content does not match its SHA-1");
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
        for (std::uint64_t i = 0; i < chunks && !stop && !full() && !sinkFailed;) {
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
            for (; i < end && !full(); ++i) {
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

} // namespace

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

} // namespace wl::core
