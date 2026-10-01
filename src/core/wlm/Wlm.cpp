#include "core/wlm/Wlm.h"

#include "base/Log.h"
#include "core/image/WimFile.h"
#include "core/image/wim/WimVerify.h"
#include "core/io/ByteSource.h"

#include <Alloc.h>
#include <Bra.h>
#include <Lzma2Dec.h>
#include <Lzma2Enc.h>

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

namespace wl::core {

namespace {

constexpr char kMagic[8] = {'W', 'L', 'M', '1', 0x1A, 0x0A, 0, 0};
constexpr std::uint32_t kHeaderSize = 512;
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kWimHeaderSize = 208;
constexpr std::size_t kWimHeaderAt = 64;
constexpr std::size_t kSniff = 4096;
enum Group : std::uint8_t { kX64 = 0, kX86 = 1, kRes = 2, kRest = 3 };
enum Filter : std::uint8_t { kNone = 0, kBranchX86 = 1 };

// ---- little-endian helpers -------------------------------------------------------------------------

template <class T>
void put(std::string& out, T value) {
    char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out.append(bytes, sizeof(T));
}

template <class T>
T get(std::string_view in, std::size_t at) {
    T value{};
    std::memcpy(&value, in.data() + at, sizeof(T));
    return value;
}

class Cursor {
public:
    explicit Cursor(std::string_view data) : m_data(data) {}
    template <class T>
    bool read(T& value) {
        if (m_at + sizeof(T) > m_data.size()) {
            return false;
        }
        value = get<T>(m_data, m_at);
        m_at += sizeof(T);
        return true;
    }
    bool bytes(void* out, std::size_t n) {
        if (m_at + n > m_data.size()) {
            return false;
        }
        std::memcpy(out, m_data.data() + m_at, n);
        m_at += n;
        return true;
    }
    bool skip(std::size_t n) {
        if (m_at + n > m_data.size()) {
            return false;
        }
        m_at += n;
        return true;
    }

private:
    std::string_view m_data;
    std::size_t m_at = 0;
};

// ---- the table ----------------------------------------------------------------------------------------

struct Block {
    std::uint64_t fileOffset = 0;
    std::uint64_t packed = 0;
    std::uint64_t plain = 0;
    std::uint8_t prop = 0;
};
struct GroupInfo {
    std::uint8_t filter = kNone;
    std::uint64_t plain = 0;
    std::vector<Block> blocks;
};
struct StreamRec {
    std::uint8_t flags = 0;
    std::uint8_t group = kRest;
    std::uint16_t part = 1;
    std::uint32_t refCount = 0;
    std::uint64_t size = 0;
    std::uint64_t groupOffset = 0;
    std::array<std::uint8_t, 20> hash{};
};
struct Table {
    std::array<GroupInfo, kWlmGroups> groups;
    std::vector<StreamRec> streams; // the WIM's lookup order
    std::string xml;                // the XML resource as stored
};

std::string serialize(const Table& t) {
    std::string out = "WLMT";
    put<std::uint32_t>(out, kVersion);
    put<std::uint32_t>(out, kWlmGroups);
    for (const auto& g : t.groups) {
        put<std::uint8_t>(out, g.filter);
        out.append(3, '\0');
        put<std::uint64_t>(out, g.plain);
        put<std::uint32_t>(out, static_cast<std::uint32_t>(g.blocks.size()));
        for (const auto& b : g.blocks) {
            put<std::uint64_t>(out, b.fileOffset);
            put<std::uint64_t>(out, b.packed);
            put<std::uint64_t>(out, b.plain);
            put<std::uint8_t>(out, b.prop);
            out.append(7, '\0');
        }
    }
    put<std::uint32_t>(out, static_cast<std::uint32_t>(t.streams.size()));
    for (const auto& s : t.streams) {
        put<std::uint8_t>(out, s.flags);
        put<std::uint8_t>(out, s.group);
        put<std::uint16_t>(out, s.part);
        put<std::uint32_t>(out, s.refCount);
        put<std::uint64_t>(out, s.size);
        put<std::uint64_t>(out, s.groupOffset);
        out.append(reinterpret_cast<const char*>(s.hash.data()), s.hash.size());
    }
    put<std::uint32_t>(out, static_cast<std::uint32_t>(t.xml.size()));
    out += t.xml;
    return out;
}

Result<Table> parseTable(std::string_view data) {
    auto broken = [] { return fail(ErrorCode::ParseError, L"the WLM table is broken", L"WLM"); };
    Cursor c(data);
    char magic[4];
    std::uint32_t version = 0;
    std::uint32_t groups = 0;
    if (!c.bytes(magic, 4) || std::memcmp(magic, "WLMT", 4) != 0 || !c.read(version) || version != kVersion ||
        !c.read(groups) || groups != kWlmGroups) {
        return broken();
    }
    Table t;
    for (auto& g : t.groups) {
        std::uint32_t blocks = 0;
        if (!c.read(g.filter) || !c.skip(3) || !c.read(g.plain) || !c.read(blocks) || blocks > (1u << 20)) {
            return broken();
        }
        g.blocks.resize(blocks);
        for (auto& b : g.blocks) {
            if (!c.read(b.fileOffset) || !c.read(b.packed) || !c.read(b.plain) || !c.read(b.prop) || !c.skip(7)) {
                return broken();
            }
        }
    }
    std::uint32_t streams = 0;
    if (!c.read(streams) || streams > (1u << 26)) {
        return broken();
    }
    t.streams.resize(streams);
    for (auto& s : t.streams) {
        if (!c.read(s.flags) || !c.read(s.group) || !c.read(s.part) || !c.read(s.refCount) || !c.read(s.size) ||
            !c.read(s.groupOffset) || !c.bytes(s.hash.data(), s.hash.size()) || s.group >= kWlmGroups) {
            return broken();
        }
    }
    std::uint32_t xml = 0;
    if (!c.read(xml)) {
        return broken();
    }
    t.xml.resize(xml);
    if (!c.bytes(t.xml.data(), xml)) {
        return broken();
    }
    return t;
}

// ---- LZMA2 (LZMA SDK) ---------------------------------------------------------------------------------

Result<std::string> lzma2Encode(const char* data, std::size_t size, std::uint32_t dict, int level, std::uint8_t& prop) {
    CLzma2EncHandle enc = Lzma2Enc_Create(&g_Alloc, &g_BigAlloc);
    if (!enc) {
        return fail(ErrorCode::Unknown, L"out of memory (LZMA2 encoder)", L"WLM");
    }
    CLzma2EncProps props;
    Lzma2EncProps_Init(&props);
    props.lzmaProps.level = level;
    props.lzmaProps.dictSize = std::max<std::uint32_t>(dict, 1u << 16);
    props.lzmaProps.fb = 273;
    props.lzmaProps.numThreads = 2; // binary-tree match finder on its own thread
    props.lzmaProps.reduceSize = size;
    props.blockSize = LZMA2_ENC_PROPS_BLOCK_SIZE_SOLID;
    props.numBlockThreads_Max = 1;
    props.numBlockThreads_Reduced = 1;
    props.numTotalThreads = 2;
    SRes rc = Lzma2Enc_SetProps(enc, &props);
    std::string out;
    if (rc == SZ_OK) {
        Lzma2Enc_SetDataSize(enc, size);
        prop = Lzma2Enc_WriteProperties(enc);
        out.resize(size + size / 32 + (1u << 16));
        std::size_t outSize = out.size();
        rc = Lzma2Enc_Encode2(enc, nullptr, reinterpret_cast<Byte*>(out.data()), &outSize, nullptr,
                              reinterpret_cast<const Byte*>(data), size, nullptr);
        out.resize(outSize);
    }
    Lzma2Enc_Destroy(enc);
    if (rc != SZ_OK) {
        return fail(ErrorCode::Unknown, std::format(L"LZMA2 encoding failed ({})", rc), L"WLM");
    }
    return out;
}

Result<void> lzma2Decode(const char* src, std::size_t srcLen, char* dst, std::size_t dstLen, std::uint8_t prop) {
    SizeT outLen = dstLen;
    SizeT inLen = srcLen;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    const SRes rc = Lzma2Decode(reinterpret_cast<Byte*>(dst), &outLen, reinterpret_cast<const Byte*>(src), &inLen, prop,
                                LZMA_FINISH_END, &status, &g_Alloc);
    if (rc != SZ_OK || outLen != dstLen) {
        return fail(ErrorCode::ParseError, std::format(L"a WLM block does not decode ({}, {} of {} bytes)", rc, outLen, dstLen),
                    L"WLM");
    }
    return {};
}

void branchEncode(char* data, std::size_t size) {
    UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
    z7_BranchConvSt_X86_Enc(reinterpret_cast<Byte*>(data), size, 0, &state);
}
void branchDecode(char* data, std::size_t size) {
    UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
    z7_BranchConvSt_X86_Dec(reinterpret_cast<Byte*>(data), size, 0, &state);
}

// ---- what a stream is ---------------------------------------------------------------------------------

std::uint8_t classify(std::string_view head, bool metadata) {
    if (metadata || head.size() < 0x40 || head[0] != 'M' || head[1] != 'Z') {
        return kRest;
    }
    const auto pe = get<std::uint32_t>(head, 0x3C);
    if (pe + 24 > head.size() || head.substr(pe, 4) != std::string_view("PE\0\0", 4)) {
        return kRest;
    }
    const auto machine = get<std::uint16_t>(head, pe + 4);
    const auto sections = get<std::uint16_t>(head, pe + 6);
    const auto optional = get<std::uint16_t>(head, pe + 20);
    bool code = false;
    for (std::uint16_t i = 0; i < sections && i < 96; ++i) {
        const std::size_t at = pe + 24 + optional + std::size_t{i} * 40;
        if (at + 40 > head.size()) {
            break;
        }
        if (get<std::uint32_t>(head, at + 36) & 0x20) { // IMAGE_SCN_CNT_CODE
            code = true;
        }
    }
    if (!code) {
        return kRes;
    }
    if (machine == 0x8664 || machine == 0xAA64) {
        return kX64;
    }
    return machine == 0x14C ? kX86 : kRest;
}

// ---- SHA-1 --------------------------------------------------------------------------------------------

class Sha1 {
public:
    Sha1() {
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0))) {
            (void)BCryptCreateHash(m_alg, &m_hash, nullptr, 0, nullptr, 0, BCRYPT_HASH_REUSABLE_FLAG);
        }
    }
    ~Sha1() {
        if (m_hash) {
            BCryptDestroyHash(m_hash);
        }
        if (m_alg) {
            BCryptCloseAlgorithmProvider(m_alg, 0);
        }
    }
    Sha1(const Sha1&) = delete;
    Sha1& operator=(const Sha1&) = delete;
    [[nodiscard]] bool usable() const noexcept { return m_hash != nullptr; }
    void update(const char* data, std::size_t size) {
        BCryptHashData(m_hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), static_cast<ULONG>(size), 0);
    }
    std::array<std::uint8_t, 20> finish() {
        std::array<std::uint8_t, 20> d{};
        BCryptFinishHash(m_hash, d.data(), static_cast<ULONG>(d.size()), 0);
        return d;
    }

private:
    BCRYPT_ALG_HANDLE m_alg = nullptr;
    BCRYPT_HASH_HANDLE m_hash = nullptr;
};

// ---- a small pool: blocks done in parallel, taken back in order ----------------------------------------

struct Job {
    std::size_t seq = 0;
    int group = 0;
    std::string input;
    std::string output;
    std::uint8_t prop = 0;
    bool done = false;
    std::optional<Error> error;
};

class Pool {
public:
    Pool(unsigned threads, std::function<void(Job&)> work) : m_work(std::move(work)) {
        for (unsigned i = 0; i < threads; ++i) {
            m_threads.emplace_back([this] { run(); });
        }
    }
    ~Pool() {
        {
            std::scoped_lock lock(m_mutex);
            m_closing = true;
        }
        m_wake.notify_all();
        for (auto& t : m_threads) {
            t.join();
        }
    }
    void submit(std::unique_ptr<Job> job) {
        {
            std::scoped_lock lock(m_mutex);
            m_queue.push_back(job.get());
            m_order.push_back(std::move(job));
        }
        m_wake.notify_all();
    }
    [[nodiscard]] std::size_t inFlight() {
        std::scoped_lock lock(m_mutex);
        return m_order.size();
    }
    // The oldest job once it is done (nullptr when none is left).
    std::unique_ptr<Job> next() {
        std::unique_lock lock(m_mutex);
        if (m_order.empty()) {
            return nullptr;
        }
        m_done.wait(lock, [this] { return m_order.front()->done; });
        auto job = std::move(m_order.front());
        m_order.pop_front();
        return job;
    }

private:
    void run() {
        for (;;) {
            Job* job = nullptr;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_closing || !m_queue.empty(); });
                if (m_queue.empty()) {
                    return;
                }
                job = m_queue.front();
                m_queue.pop_front();
            }
            m_work(*job);
            {
                std::scoped_lock lock(m_mutex);
                job->done = true;
            }
            m_done.notify_all();
        }
    }
    std::function<void(Job&)> m_work;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_done;
    std::deque<Job*> m_queue;
    std::deque<std::unique_ptr<Job>> m_order;
    std::vector<std::thread> m_threads;
    bool m_closing = false;
};

unsigned autoThreads(std::uint64_t perJobBytes) {
    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);
    const std::uint64_t spare = mem.ullAvailPhys > (3ull << 30) ? mem.ullAvailPhys - (3ull << 30) : 0;
    const unsigned byMemory = static_cast<unsigned>(std::max<std::uint64_t>(spare / std::max<std::uint64_t>(perJobBytes, 1), 1));
    const unsigned byCores = std::max(1u, std::thread::hardware_concurrency() / 2);
    return std::clamp(std::min(byMemory, byCores), 1u, 8u);
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

Result<std::string> readAll(const ByteSource& src, std::uint64_t offset, std::uint64_t size) {
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        if (auto r = src.read(offset, std::span(reinterpret_cast<std::byte*>(bytes.data()), bytes.size())); !r) {
            return std::unexpected(r.error());
        }
    }
    return bytes;
}

struct Header {
    std::uint64_t tableOffset = 0;
    std::uint64_t tablePacked = 0;
    std::uint64_t tablePlain = 0;
    std::uint8_t tableProp = 0;
    std::uint64_t plainBytes = 0;
    std::string wimHeader; // 208 bytes
};

Result<Header> readHeader(std::ifstream& in, std::uint64_t fileSize) {
    std::string raw(kHeaderSize, '\0');
    in.seekg(0);
    in.read(raw.data(), kHeaderSize);
    if (!in || std::memcmp(raw.data(), kMagic, sizeof(kMagic)) != 0) {
        return fail(ErrorCode::ParseError, L"not a WLM file", L"WLM");
    }
    if (get<std::uint32_t>(raw, 8) != kHeaderSize || get<std::uint32_t>(raw, 12) != kVersion) {
        return fail(ErrorCode::Unsupported, L"a WLM version this build does not read", L"WLM");
    }
    Header h;
    h.tableOffset = get<std::uint64_t>(raw, 16);
    h.tablePacked = get<std::uint64_t>(raw, 24);
    h.tablePlain = get<std::uint64_t>(raw, 32);
    h.tableProp = get<std::uint8_t>(raw, 40);
    h.plainBytes = get<std::uint64_t>(raw, 48);
    h.wimHeader = raw.substr(kWimHeaderAt, kWimHeaderSize);
    if (h.tableOffset < kHeaderSize || h.tableOffset > fileSize || h.tablePacked > fileSize - h.tableOffset ||
        h.tablePlain > (1ull << 31)) {
        return fail(ErrorCode::ParseError, L"the WLM header is broken", L"WLM");
    }
    return h;
}

Result<Table> readTableOf(std::ifstream& in, const Header& h) {
    std::string packed(static_cast<std::size_t>(h.tablePacked), '\0');
    in.seekg(static_cast<std::streamoff>(h.tableOffset));
    in.read(packed.data(), static_cast<std::streamsize>(packed.size()));
    if (!in) {
        return fail(ErrorCode::IoError, L"the WLM table cannot be read", L"WLM");
    }
    std::string plain(static_cast<std::size_t>(h.tablePlain), '\0');
    if (auto d = lzma2Decode(packed.data(), packed.size(), plain.data(), plain.size(), h.tableProp); !d) {
        return std::unexpected(d.error());
    }
    return parseTable(plain);
}

WlmReport reportOf(const Table& t, std::uint64_t fileBytes) {
    WlmReport r;
    r.fileBytes = fileBytes;
    r.streams = t.streams.size();
    for (std::size_t g = 0; g < kWlmGroups; ++g) {
        r.groups[g].plain = t.groups[g].plain;
        r.groups[g].blocks = static_cast<std::uint32_t>(t.groups[g].blocks.size());
        for (const auto& b : t.groups[g].blocks) {
            r.groups[g].packed += b.packed;
        }
        r.plainBytes += t.groups[g].plain;
    }
    for (const auto& s : t.streams) {
        ++r.groups[s.group].streams;
    }
    return r;
}

} // namespace

const wchar_t* wlmGroupName(int group) noexcept {
    switch (group) {
    case kX64: return L"x64 code";
    case kX86: return L"x86 code";
    case kRes: return L"resources";
    default: return L"other";
    }
}

bool isWlmFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    char magic[sizeof(kMagic)]{};
    in.read(magic, sizeof(magic));
    return in && std::memcmp(magic, kMagic, sizeof(kMagic)) == 0;
}

// ---- pack ---------------------------------------------------------------------------------------------

Result<WlmReport> packWlm(const std::filesystem::path& wimPath, const std::filesystem::path& wlmPath,
                          const WlmPackOptions& options, const TaskContext& task) {
    const auto started = std::chrono::steady_clock::now();
    auto wim = DiskFile::open(wimPath);
    if (!wim) {
        return std::unexpected(wim.error());
    }
    auto table = readWimStreamTable(**wim);
    if (!table) {
        return std::unexpected(table.error());
    }
    if (table->header.totalParts != 1) {
        return fail(ErrorCode::Unsupported, L"a split WIM cannot be packed: join it first", wimPath.wstring());
    }
    auto rawHeader = readAll(**wim, 0, kWimHeaderSize);
    auto xml = readAll(**wim, table->header.xmlOffset, table->header.xmlSize);
    if (!rawHeader || !xml) {
        return std::unexpected(!rawHeader ? rawHeader.error() : xml.error());
    }

    const auto& entries = table->entries;
    std::vector<std::size_t> order(entries.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return entries[a].offset < entries[b].offset; });

    // 1. What each stream is (its first 4 KiB).
    WimStreamReader reader(**wim, *table);
    Table out;
    out.xml = std::move(*xml);
    out.streams.resize(entries.size());
    std::uint64_t totalPlain = 0;
    for (const std::size_t i : order) {
        if (auto cancelled = task.cancel.check(L"WLM"); !cancelled) {
            return std::unexpected(cancelled.error());
        }
        std::string head;
        auto sniffed = reader.read(entries[i], [&](std::span<const std::byte> d) {
            head.append(reinterpret_cast<const char*>(d.data()), d.size());
            return true;
        }, kSniff);
        if (!sniffed) {
            return std::unexpected(sniffed.error());
        }
        auto& rec = out.streams[i];
        rec.flags = entries[i].flags;
        rec.group = classify(head, entries[i].metadata());
        rec.part = entries[i].part;
        rec.refCount = entries[i].refCount;
        rec.size = entries[i].original;
        rec.hash = entries[i].hash;
        totalPlain += entries[i].original;
    }

    // 2. Each group's streams back to back, cut into blocks, compressed in parallel.
    const std::uint64_t blockSize = std::uint64_t{std::clamp<std::uint32_t>(options.blockMiB, 1, 1536)} << 20;
    const unsigned threads = options.threads ? options.threads : autoThreads(blockSize * 12 + blockSize * 2);
    std::ofstream file(wlmPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCode::IoError, L"cannot create the WLM file", wlmPath.wstring());
    }
    file.write(std::string(kHeaderSize, '\0').data(), kHeaderSize);
    std::uint64_t position = kHeaderSize;
    std::optional<Error> failure;
    Pool pool(threads, [&](Job& job) {
        if (out.groups[static_cast<std::size_t>(job.group)].filter == kBranchX86) {
            branchEncode(job.input.data(), job.input.size());
        }
        auto packed = lzma2Encode(job.input.data(), job.input.size(), static_cast<std::uint32_t>(blockSize), options.level, job.prop);
        if (!packed) {
            job.error = packed.error();
        } else {
            job.output = std::move(*packed);
        }
        std::string().swap(job.input); // the plain block is no longer needed
    });
    auto writeDone = [&](std::unique_ptr<Job> job) {
        if (job->error) {
            failure = job->error;
            return;
        }
        auto& group = out.groups[static_cast<std::size_t>(job->group)];
        Block& block = group.blocks[job->seq];
        block.fileOffset = position;
        block.packed = job->output.size();
        block.prop = job->prop;
        file.write(job->output.data(), static_cast<std::streamsize>(job->output.size()));
        position += job->output.size();
    };
    std::uint64_t readPlain = 0;
    for (int g = 0; g < kWlmGroups && !failure; ++g) {
        auto& group = out.groups[static_cast<std::size_t>(g)];
        group.filter = g == kX64 || g == kX86 ? kBranchX86 : kNone;
        auto block = std::make_unique<Job>();
        auto flush = [&] {
            if (block->input.empty()) {
                return;
            }
            block->seq = group.blocks.size();
            block->group = g;
            group.blocks.push_back(Block{0, 0, block->input.size(), 0});
            pool.submit(std::move(block));
            block = std::make_unique<Job>();
            while (pool.inFlight() > threads && !failure) {
                writeDone(pool.next());
            }
        };
        for (const std::size_t i : order) {
            auto& rec = out.streams[i];
            if (rec.group != g) {
                continue;
            }
            rec.groupOffset = group.plain;
            group.plain += rec.size;
            auto copied = reader.read(entries[i], [&](std::span<const std::byte> d) {
                const char* p = reinterpret_cast<const char*>(d.data());
                std::size_t left = d.size();
                while (left > 0) {
                    if (block->input.capacity() < blockSize) {
                        block->input.reserve(static_cast<std::size_t>(blockSize));
                    }
                    const std::size_t room = static_cast<std::size_t>(blockSize - block->input.size());
                    const std::size_t n = std::min(room, left);
                    block->input.append(p, n);
                    p += n;
                    left -= n;
                    if (block->input.size() == blockSize) {
                        flush();
                    }
                }
                readPlain += d.size();
                task.report(totalPlain ? static_cast<double>(readPlain) / static_cast<double>(totalPlain) : 1.0, L"WLM");
                return !failure && !task.cancel.cancelled();
            });
            if (!copied) {
                failure = task.cancel.cancelled() ? Error{ErrorCode::Cancelled, L"cancelled", L"WLM"} : copied.error();
                break;
            }
        }
        if (!failure) {
            flush();
        }
    }
    while (auto job = pool.next()) {
        if (!failure) {
            writeDone(std::move(job));
        }
    }
    if (failure) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(wlmPath, ec);
        return std::unexpected(*failure);
    }

    // 3. The table at the end, the header in front.
    const std::string plainTable = serialize(out);
    std::uint8_t tableProp = 0;
    auto packedTable = lzma2Encode(plainTable.data(), plainTable.size(), 1u << 24, options.level, tableProp);
    if (!packedTable) {
        return std::unexpected(packedTable.error());
    }
    file.write(packedTable->data(), static_cast<std::streamsize>(packedTable->size()));
    std::string header(kHeaderSize, '\0');
    std::memcpy(header.data(), kMagic, sizeof(kMagic));
    auto set = [&](std::size_t at, auto value) { std::memcpy(header.data() + at, &value, sizeof(value)); };
    set(8, kHeaderSize);
    set(12, kVersion);
    set(16, position);
    set(24, static_cast<std::uint64_t>(packedTable->size()));
    set(32, static_cast<std::uint64_t>(plainTable.size()));
    set(40, tableProp);
    set(48, totalPlain);
    std::memcpy(header.data() + kWimHeaderAt, rawHeader->data(), kWimHeaderSize);
    file.seekp(0);
    file.write(header.data(), kHeaderSize);
    file.close();
    if (!file) {
        return fail(ErrorCode::IoError, L"the WLM file could not be written", wlmPath.wstring());
    }
    WlmReport report = reportOf(out, position + packedTable->size());
    report.threads = threads;
    report.seconds = secondsSince(started);
    log::info("wlm", std::format(L"packed {} -> {}: {} streams, {} -> {} bytes, {} thread(s), {:.0f} s", wimPath.wstring(),
                                 wlmPath.wstring(), report.streams, report.plainBytes, report.fileBytes, threads, report.seconds));
    return report;
}

// ---- info / unpack -----------------------------------------------------------------------------------

Result<WlmReport> readWlmInfo(const std::filesystem::path& wlmPath) {
    std::ifstream in(wlmPath, std::ios::binary);
    std::error_code ec;
    const auto size = std::filesystem::file_size(wlmPath, ec);
    if (!in || ec) {
        return fail(ErrorCode::NotFound, L"WLM file not found", wlmPath.wstring());
    }
    auto header = readHeader(in, size);
    if (!header) {
        return std::unexpected(header.error());
    }
    auto table = readTableOf(in, *header);
    if (!table) {
        return std::unexpected(table.error());
    }
    return reportOf(*table, size);
}

Result<WlmReport> unpackWlm(const std::filesystem::path& wlmPath, const std::filesystem::path& wimPath,
                            const TaskContext& task, unsigned threads) {
    const auto started = std::chrono::steady_clock::now();
    std::ifstream in(wlmPath, std::ios::binary);
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(wlmPath, ec);
    if (!in || ec) {
        return fail(ErrorCode::NotFound, L"WLM file not found", wlmPath.wstring());
    }
    auto header = readHeader(in, fileSize);
    if (!header) {
        return std::unexpected(header.error());
    }
    auto table = readTableOf(in, *header);
    if (!table) {
        return std::unexpected(table.error());
    }
    // Every group's blocks add up, every stream sits inside its group, back to back.
    std::array<std::vector<std::size_t>, kWlmGroups> members;
    for (std::size_t i = 0; i < table->streams.size(); ++i) {
        members[table->streams[i].group].push_back(i);
    }
    for (std::size_t g = 0; g < kWlmGroups; ++g) {
        std::uint64_t sum = 0;
        for (const auto& b : table->groups[g].blocks) {
            if (b.fileOffset < kHeaderSize || b.fileOffset > fileSize || b.packed > fileSize - b.fileOffset) {
                return fail(ErrorCode::ParseError, L"a WLM block lies outside the file (truncated?)", wlmPath.wstring());
            }
            sum += b.plain;
        }
        std::ranges::sort(members[g], {}, [&](std::size_t i) { return table->streams[i].groupOffset; });
        std::uint64_t next = 0;
        for (const std::size_t i : members[g]) {
            if (table->streams[i].groupOffset != next) {
                return fail(ErrorCode::ParseError, L"the WLM stream table is inconsistent", wlmPath.wstring());
            }
            next += table->streams[i].size;
        }
        if (sum != table->groups[g].plain || next != sum) {
            return fail(ErrorCode::ParseError, L"the WLM group sizes do not add up", wlmPath.wstring());
        }
    }

    std::ofstream out(wimPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return fail(ErrorCode::IoError, L"cannot create the WIM", wimPath.wstring());
    }
    std::vector<char> outBuffer(4u << 20);
    out.rdbuf()->pubsetbuf(outBuffer.data(), static_cast<std::streamsize>(outBuffer.size()));
    out.write(std::string(kWimHeaderSize, '\0').data(), kWimHeaderSize);
    std::array<std::uint64_t, kWlmGroups> base{};
    {
        std::uint64_t at = kWimHeaderSize;
        for (std::size_t g = 0; g < kWlmGroups; ++g) {
            base[g] = at;
            at += table->groups[g].plain;
        }
    }

    Sha1 sha;
    if (!sha.usable()) {
        return fail(ErrorCode::Unknown, L"SHA-1 is not available", L"WLM");
    }
    const unsigned workers = threads ? threads : std::clamp(std::thread::hardware_concurrency() / 2, 1u, 6u);
    std::optional<Error> failure;
    std::mutex fileMutex; // blocks are read from the one input stream
    Pool pool(workers, [&](Job& job) {
        const auto& g = table->groups[static_cast<std::size_t>(job.group)];
        const Block& b = g.blocks[job.seq];
        std::string packed(static_cast<std::size_t>(b.packed), '\0');
        {
            std::scoped_lock lock(fileMutex);
            in.seekg(static_cast<std::streamoff>(b.fileOffset));
            in.read(packed.data(), static_cast<std::streamsize>(packed.size()));
            if (!in) {
                job.error = Error{ErrorCode::IoError, L"a WLM block cannot be read", wlmPath.wstring()};
                return;
            }
        }
        job.output.resize(static_cast<std::size_t>(b.plain));
        if (auto d = lzma2Decode(packed.data(), packed.size(), job.output.data(), job.output.size(), b.prop); !d) {
            job.error = d.error();
            return;
        }
        if (g.filter == kBranchX86) {
            branchDecode(job.output.data(), job.output.size());
        }
    });

    std::uint64_t written = 0;
    std::uint64_t checked = 0;
    for (int g = 0; g < kWlmGroups && !failure; ++g) {
        const auto& group = table->groups[static_cast<std::size_t>(g)];
        std::size_t member = 0;    // the stream the bytes belong to
        std::uint64_t inStream = 0; // bytes of it seen
        auto feed = [&](const char* p, std::size_t n) {
            while (n > 0 || (member < members[g].size() && table->streams[members[g][member]].size == inStream)) {
                if (member >= members[g].size()) {
                    break;
                }
                const auto& s = table->streams[members[g][member]];
                const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(n, s.size - inStream));
                sha.update(p, take);
                p += take;
                n -= take;
                inStream += take;
                if (inStream == s.size) {
                    if (sha.finish() != s.hash) {
                        failure = Error{ErrorCode::ParseError, L"a stream does not match its SHA-1 after unpacking",
                                        std::format(L"stream {}", members[g][member])};
                        return;
                    }
                    ++checked;
                    ++member;
                    inStream = 0;
                }
            }
        };
        feed(nullptr, 0); // empty streams at the start of a group
        auto drainOne = [&] {
            auto job = pool.next();
            if (!job) {
                return;
            }
            if (job->error) {
                failure = job->error;
                return;
            }
            out.write(job->output.data(), static_cast<std::streamsize>(job->output.size()));
            feed(job->output.data(), job->output.size());
            written += job->output.size();
            task.report(header->plainBytes ? static_cast<double>(written) / static_cast<double>(header->plainBytes) : 1.0, L"WLM");
        };
        for (std::size_t b = 0; b < group.blocks.size() && !failure; ++b) {
            if (task.cancel.cancelled()) {
                failure = Error{ErrorCode::Cancelled, L"cancelled", L"WLM"};
                break;
            }
            auto job = std::make_unique<Job>();
            job->seq = b;
            job->group = g;
            pool.submit(std::move(job));
            while (pool.inFlight() > workers && !failure) {
                drainOne();
            }
        }
        while (pool.inFlight() > 0) {
            if (failure) {
                (void)pool.next();
            } else {
                drainOne();
            }
        }
        if (!failure && member != members[g].size()) {
            failure = Error{ErrorCode::ParseError, L"the WLM group ended before its streams did", wlmPath.wstring()};
        }
    }
    if (failure) {
        out.close();
        std::filesystem::remove(wimPath, ec);
        return std::unexpected(*failure);
    }

    // Lookup table in the original order, then the XML, then the header.
    const std::string& src = header->wimHeader;
    auto reshdr = [](std::string& b, std::uint64_t size, std::uint8_t flags, std::uint64_t offset, std::uint64_t original) {
        put<std::uint64_t>(b, (size & 0x00FFFFFFFFFFFFFFull) | (std::uint64_t{flags} << 56));
        put<std::uint64_t>(b, offset);
        put<std::uint64_t>(b, original);
    };
    std::string lookup;
    lookup.reserve(table->streams.size() * 50);
    std::string bootMetadata(24, '\0');
    const std::uint32_t bootIndex = get<std::uint32_t>(src, 120);
    std::uint32_t metadataSeen = 0;
    for (const auto& s : table->streams) {
        const std::uint8_t flags = static_cast<std::uint8_t>(s.flags & ~0x14); // stored plain, not solid
        const std::uint64_t offset = base[s.group] + s.groupOffset;
        reshdr(lookup, s.size, flags, offset, s.size);
        put<std::uint16_t>(lookup, 1);
        put<std::uint32_t>(lookup, s.refCount);
        lookup.append(reinterpret_cast<const char*>(s.hash.data()), s.hash.size());
        if (s.flags & 0x02) {
            if (++metadataSeen == bootIndex) {
                bootMetadata.clear();
                reshdr(bootMetadata, s.size, flags, offset, s.size);
            }
        }
    }
    const std::uint64_t lookupOffset = kWimHeaderSize + written;
    out.write(lookup.data(), static_cast<std::streamsize>(lookup.size()));
    const std::uint64_t xmlOffset = lookupOffset + lookup.size();
    out.write(table->xml.data(), static_cast<std::streamsize>(table->xml.size()));

    std::string wimHeader;
    wimHeader.append("MSWIM\0\0\0", 8);
    put<std::uint32_t>(wimHeader, static_cast<std::uint32_t>(kWimHeaderSize));
    put<std::uint32_t>(wimHeader, 0x10D00); // a plain WIM
    // Compression, spanned and write-in-progress flags off; the others (reparse fix …) kept.
    const std::uint32_t flags = get<std::uint32_t>(src, 16) & ~(0x2u | 0x8u | 0x40u | 0x00FF0000u);
    put<std::uint32_t>(wimHeader, flags);
    put<std::uint32_t>(wimHeader, 0); // chunk size: no compression
    wimHeader.append(src.substr(24, 16)); // GUID
    put<std::uint16_t>(wimHeader, 1);
    put<std::uint16_t>(wimHeader, 1);
    wimHeader.append(src.substr(44, 4)); // image count
    reshdr(wimHeader, lookup.size(), static_cast<std::uint8_t>(get<std::uint8_t>(src, 55) & ~0x04), lookupOffset, lookup.size());
    reshdr(wimHeader, table->xml.size(), static_cast<std::uint8_t>(get<std::uint8_t>(src, 79) & ~0x04), xmlOffset,
           table->xml.size());
    wimHeader += bootMetadata;
    put<std::uint32_t>(wimHeader, bootIndex);
    wimHeader.append(24, '\0'); // no integrity table
    wimHeader.resize(kWimHeaderSize, '\0');
    out.seekp(0);
    out.write(wimHeader.data(), static_cast<std::streamsize>(wimHeader.size()));
    out.close();
    if (!out) {
        return fail(ErrorCode::IoError, L"the WIM could not be written", wimPath.wstring());
    }
    WlmReport report = reportOf(*table, fileSize);
    report.threads = workers;
    report.seconds = secondsSince(started);
    log::info("wlm", std::format(L"unpacked {} -> {}: {} streams checked, {} bytes, {:.0f} s", wlmPath.wstring(), wimPath.wstring(),
                                 checked, written, report.seconds));
    return report;
}

} // namespace wl::core
