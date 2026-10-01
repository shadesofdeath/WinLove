#include "core/wlm/Wlm.h"

#include "base/Log.h"
#include "core/image/WimFile.h"
#include "core/image/wim/WimGapi.h"
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
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace wl::core {

namespace {

constexpr char kMagic[8] = {'W', 'L', 'M', '1', 0x1A, 0x0A, 0, 0};
constexpr std::uint32_t kHeaderSize = 512;
constexpr std::uint32_t kVersion = 2;
constexpr std::size_t kWimHeaderSize = 208;
constexpr std::size_t kWimHeaderAt = 64;
constexpr std::size_t kSniff = 4096;
constexpr std::uint32_t kNoStream = 0xFFFFFFFFu;
constexpr std::size_t kChunk = 4u << 20;
enum Group : std::uint8_t { kX64 = 0, kX86 = 1, kRes = 2, kRest = 3, kNoGroup = 0xFF };
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
    std::vector<Block> blocks; // v2: one, the whole group as a single LZMA2 stream
};
struct StreamRec {
    std::uint8_t flags = 0;
    std::uint8_t group = kRest; // kNoGroup: rebuilt (nested WIM) or a duplicate
    std::uint16_t part = 1;
    std::uint32_t refCount = 0;
    std::uint64_t size = 0;
    std::uint64_t groupOffset = 0;
    std::array<std::uint8_t, 20> hash{};
    std::uint16_t owner = 0;         // 0: the WIM itself; k: the k-th nested WIM
    std::uint16_t nested = 0;        // k: this stream IS the k-th nested WIM (rebuilt on unpack)
    std::uint32_t dupOf = kNoStream; // same bytes as this stream of the outer WIM
};
struct NestedRec {
    std::uint32_t outerStream = 0;
    std::string wimHeader; // 208 bytes
    std::string xml;
};
struct Table {
    std::array<GroupInfo, kWlmGroups> groups;
    std::vector<StreamRec> streams; // outer lookup order, then each nested WIM's in its lookup order
    std::string xml;
    std::vector<NestedRec> nested;
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
        put<std::uint16_t>(out, s.owner);
        put<std::uint16_t>(out, s.nested);
        put<std::uint32_t>(out, s.dupOf);
    }
    put<std::uint32_t>(out, static_cast<std::uint32_t>(t.xml.size()));
    out += t.xml;
    put<std::uint32_t>(out, static_cast<std::uint32_t>(t.nested.size()));
    for (const auto& n : t.nested) {
        put<std::uint32_t>(out, n.outerStream);
        out += n.wimHeader;
        put<std::uint32_t>(out, static_cast<std::uint32_t>(n.xml.size()));
        out += n.xml;
    }
    return out;
}

Result<Table> parseTable(std::string_view data) {
    auto broken = [] { return fail(ErrorCode::ParseError, L"the WLM table is broken", L"WLM"); };
    Cursor c(data);
    char magic[4];
    std::uint32_t version = 0;
    std::uint32_t groups = 0;
    if (!c.bytes(magic, 4) || std::memcmp(magic, "WLMT", 4) != 0 || !c.read(version)) {
        return broken();
    }
    if (version != kVersion) {
        return fail(ErrorCode::Unsupported, L"a WLM file of another version: pack it again with this build", L"WLM");
    }
    if (!c.read(groups) || groups != kWlmGroups) {
        return broken();
    }
    Table t;
    for (auto& g : t.groups) {
        std::uint32_t blocks = 0;
        if (!c.read(g.filter) || !c.skip(3) || !c.read(g.plain) || !c.read(blocks) || blocks > 1024) {
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
            !c.read(s.groupOffset) || !c.bytes(s.hash.data(), s.hash.size()) || !c.read(s.owner) || !c.read(s.nested) ||
            !c.read(s.dupOf) || (s.group >= kWlmGroups && s.group != kNoGroup)) {
            return broken();
        }
    }
    std::uint32_t xml = 0;
    if (!c.read(xml)) {
        return broken();
    }
    t.xml.resize(xml);
    std::uint32_t nested = 0;
    if (!c.bytes(t.xml.data(), xml) || !c.read(nested) || nested > 64) {
        return broken();
    }
    t.nested.resize(nested);
    for (auto& n : t.nested) {
        n.wimHeader.resize(kWimHeaderSize);
        std::uint32_t size = 0;
        if (!c.read(n.outerStream) || !c.bytes(n.wimHeader.data(), kWimHeaderSize) || !c.read(size)) {
            return broken();
        }
        n.xml.resize(size);
        if (!c.bytes(n.xml.data(), size) || n.outerStream >= t.streams.size()) {
            return broken();
        }
    }
    for (const auto& s : t.streams) {
        if (s.owner > t.nested.size() || s.nested > t.nested.size() || (s.dupOf != kNoStream && s.dupOf >= t.streams.size())) {
            return broken();
        }
    }
    return t;
}

// ---- LZMA2: one-shot for the table, streamed for the groups -------------------------------------------

void setProps(CLzma2EncProps& props, std::uint64_t size, std::uint32_t dict, int level) {
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
}

Result<std::string> lzma2Encode(const char* data, std::size_t size, std::uint8_t& prop) {
    CLzma2EncHandle enc = Lzma2Enc_Create(&g_Alloc, &g_BigAlloc);
    if (!enc) {
        return fail(ErrorCode::Unknown, L"out of memory (LZMA2 encoder)", L"WLM");
    }
    CLzma2EncProps props;
    setProps(props, size, 1u << 24, 9);
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
        return fail(ErrorCode::ParseError, L"the WLM table does not decode", L"WLM");
    }
    return {};
}

// A bounded byte pipe: the WIM reader pushes, the LZMA2 encoder (its own thread) pulls.
class Pipe {
public:
    explicit Pipe(std::size_t capacity) : m_capacity(capacity) {}
    // false once the encoder side gave up
    bool push(const char* data, std::size_t size) {
        std::unique_lock lock(m_mutex);
        m_space.wait(lock, [&] { return m_held < m_capacity || m_aborted; });
        if (m_aborted) {
            return false;
        }
        m_chunks.emplace_back(data, size);
        m_held += size;
        m_data.notify_all();
        return true;
    }
    void close() {
        std::scoped_lock lock(m_mutex);
        m_closed = true;
        m_data.notify_all();
    }
    void abort() {
        std::scoped_lock lock(m_mutex);
        m_aborted = true;
        m_space.notify_all();
        m_data.notify_all();
    }
    // 0 = the end
    std::size_t read(char* out, std::size_t size) {
        std::unique_lock lock(m_mutex);
        m_data.wait(lock, [&] { return !m_chunks.empty() || m_closed || m_aborted; });
        std::size_t done = 0;
        while (done < size && !m_chunks.empty()) {
            auto& front = m_chunks.front();
            const std::size_t n = std::min(size - done, front.size() - m_offset);
            std::memcpy(out + done, front.data() + m_offset, n);
            done += n;
            m_offset += n;
            if (m_offset == front.size()) {
                m_held -= front.size();
                m_chunks.pop_front();
                m_offset = 0;
            }
        }
        m_space.notify_all();
        return done;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_space;
    std::condition_variable m_data;
    std::deque<std::string> m_chunks;
    std::size_t m_offset = 0;
    std::size_t m_held = 0;
    std::size_t m_capacity;
    bool m_closed = false;
    bool m_aborted = false;
};

struct PipeIn {
    ISeqInStream vt;
    Pipe* pipe;
};
SRes pipeRead(ISeqInStreamPtr p, void* buf, size_t* size) {
    const auto* self = reinterpret_cast<const PipeIn*>(p);
    *size = self->pipe->read(static_cast<char*>(buf), *size);
    return SZ_OK;
}

struct FileOut {
    ISeqOutStream vt;
    std::ofstream* file;
    std::uint64_t* written;
};
size_t fileWrite(ISeqOutStreamPtr p, const void* buf, size_t size) {
    const auto* self = reinterpret_cast<const FileOut*>(p);
    self->file->write(static_cast<const char*>(buf), static_cast<std::streamsize>(size));
    if (!*self->file) {
        return 0;
    }
    *self->written += size;
    return size;
}

// x86 branch filter over a stream: up to 4 trailing bytes wait for the next piece (the converter
// needs to see a whole call), the last ones stay as they are — the same on both sides.
class BranchStream {
public:
    explicit BranchStream(bool encode) : m_encode(encode) {}
    template <class Sink>
    void feed(const char* data, std::size_t size, Sink&& sink) {
        m_buffer.append(data, size);
        Byte* begin = reinterpret_cast<Byte*>(m_buffer.data());
        Byte* end = m_encode ? z7_BranchConvSt_X86_Enc(begin, m_buffer.size(), m_pc, &m_state)
                             : z7_BranchConvSt_X86_Dec(begin, m_buffer.size(), m_pc, &m_state);
        const std::size_t done = static_cast<std::size_t>(end - begin);
        sink(m_buffer.data(), done);
        m_pc += static_cast<UInt32>(done);
        m_buffer.erase(0, done);
    }
    template <class Sink>
    void finish(Sink&& sink) {
        sink(m_buffer.data(), m_buffer.size());
        m_buffer.clear();
    }

private:
    bool m_encode;
    std::string m_buffer;
    UInt32 m_pc = 0;
    UInt32 m_state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
};

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
        if (size > 0) {
            BCryptHashData(m_hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), static_cast<ULONG>(size), 0);
        }
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

Result<Header> readHeader(std::istream& in, std::uint64_t fileSize) {
    std::string raw(kHeaderSize, '\0');
    in.seekg(0);
    in.read(raw.data(), kHeaderSize);
    if (!in || std::memcmp(raw.data(), kMagic, sizeof(kMagic)) != 0) {
        return fail(ErrorCode::ParseError, L"not a WLM file", L"WLM");
    }
    if (get<std::uint32_t>(raw, 8) != kHeaderSize || get<std::uint32_t>(raw, 12) != kVersion) {
        return fail(ErrorCode::Unsupported, L"a WLM file of another version: pack it again with this build", L"WLM");
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

Result<Table> readTableOf(std::istream& in, const Header& h) {
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
        if (s.group < kWlmGroups) {
            ++r.groups[s.group].streams;
        }
        r.duplicates += s.dupOf != kNoStream ? 1 : 0;
    }
    r.nested = t.nested.size();
    return r;
}

// The tail of a plain WIM (lookup table, XML, header) once its streams are in `file`.
struct Placed {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::array<std::uint8_t, 20> hash{};
};
Result<void> writeWimTail(std::fstream& file, std::uint64_t end, const std::vector<std::size_t>& order, const Table& t,
                          const std::vector<Placed>& placed, std::string_view header208, std::string_view xml) {
    auto reshdr = [](std::string& b, std::uint64_t size, std::uint8_t flags, std::uint64_t offset, std::uint64_t original) {
        put<std::uint64_t>(b, (size & 0x00FFFFFFFFFFFFFFull) | (std::uint64_t{flags} << 56));
        put<std::uint64_t>(b, offset);
        put<std::uint64_t>(b, original);
    };
    std::string lookup;
    lookup.reserve(order.size() * 50);
    std::string bootMetadata(24, '\0');
    const std::uint32_t bootIndex = get<std::uint32_t>(header208, 120);
    std::uint32_t metadataSeen = 0;
    for (const std::size_t i : order) {
        const auto& s = t.streams[i];
        const auto& p = placed[i];
        const std::uint8_t flags = static_cast<std::uint8_t>(s.flags & ~0x14); // stored plain, not solid
        reshdr(lookup, p.size, flags, p.offset, p.size);
        put<std::uint16_t>(lookup, 1);
        put<std::uint32_t>(lookup, s.refCount);
        lookup.append(reinterpret_cast<const char*>(p.hash.data()), p.hash.size());
        if ((s.flags & 0x02) && ++metadataSeen == bootIndex) {
            bootMetadata.clear();
            reshdr(bootMetadata, p.size, flags, p.offset, p.size);
        }
    }
    file.seekp(static_cast<std::streamoff>(end));
    file.write(lookup.data(), static_cast<std::streamsize>(lookup.size()));
    const std::uint64_t xmlOffset = end + lookup.size();
    file.write(xml.data(), static_cast<std::streamsize>(xml.size()));

    std::string h;
    h.append("MSWIM\0\0\0", 8);
    put<std::uint32_t>(h, static_cast<std::uint32_t>(kWimHeaderSize));
    put<std::uint32_t>(h, 0x10D00); // a plain WIM
    // Compression, spanned and write-in-progress flags off; the others (reparse fix …) kept.
    put<std::uint32_t>(h, get<std::uint32_t>(header208, 16) & ~(0x2u | 0x8u | 0x40u | 0x00FF0000u));
    put<std::uint32_t>(h, 0);           // chunk size: no compression
    h.append(header208.substr(24, 16)); // GUID
    put<std::uint16_t>(h, 1);
    put<std::uint16_t>(h, 1);
    h.append(header208.substr(44, 4)); // image count
    reshdr(h, lookup.size(), static_cast<std::uint8_t>(get<std::uint8_t>(header208, 55) & ~0x04), end, lookup.size());
    reshdr(h, xml.size(), static_cast<std::uint8_t>(get<std::uint8_t>(header208, 79) & ~0x04), xmlOffset, xml.size());
    h += bootMetadata;
    put<std::uint32_t>(h, bootIndex);
    h.append(24, '\0'); // no integrity table
    h.resize(kWimHeaderSize, '\0');
    file.seekp(0);
    file.write(h.data(), static_cast<std::streamsize>(h.size()));
    file.flush();
    if (!file) {
        return fail(ErrorCode::IoError, L"the WIM could not be written", L"WLM");
    }
    return {};
}

// A WIM the pack reads streams from: the source, or a WIM inside it (extracted to a temporary file).
struct SourceWim {
    std::filesystem::path temporary; // empty for the source itself
    std::unique_ptr<DiskFile> file;
    WimStreamTable table;
    std::unique_ptr<WimStreamReader> reader;
    std::size_t first = 0; // its first record in Table::streams
};

Result<std::string> sniff(SourceWim& src, const WimStreamEntry& entry) {
    std::string head;
    auto read = src.reader->read(entry, [&](std::span<const std::byte> d) {
        head.append(reinterpret_cast<const char*>(d.data()), d.size());
        return true;
    }, kSniff);
    if (!read) {
        return std::unexpected(read.error());
    }
    return head;
}

std::vector<std::size_t> byOffset(const std::vector<WimStreamEntry>& entries) {
    std::vector<std::size_t> order(entries.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return entries[a].offset < entries[b].offset; });
    return order;
}

class MemorySource final : public ByteSource {
public:
    // `claimed`: the size the header's offsets are checked against (the WIM it came from is not here).
    MemorySource(std::string bytes, std::uint64_t claimed) : m_bytes(std::move(bytes)), m_claimed(claimed) {}
    [[nodiscard]] std::uint64_t size() const override { return m_claimed; }
    [[nodiscard]] Result<void> read(std::uint64_t offset, std::span<std::byte> out) const override {
        if (offset > m_bytes.size() || out.size() > m_bytes.size() - offset) {
            return fail(ErrorCode::ParseError, L"read past the end", L"WLM");
        }
        std::memcpy(out.data(), m_bytes.data() + offset, out.size());
        return {};
    }

private:
    std::string m_bytes;
    std::uint64_t m_claimed = 0;
};

Result<std::pair<Header, Table>> openWlm(std::ifstream& in, const std::filesystem::path& wlmPath, std::uint64_t& size) {
    std::error_code ec;
    size = std::filesystem::file_size(wlmPath, ec);
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
    return std::pair{std::move(*header), std::move(*table)};
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
    std::vector<SourceWim> sources;
    struct Cleanup {
        std::vector<SourceWim>& sources;
        ~Cleanup() {
            for (auto& s : sources) {
                s.reader.reset();
                s.file.reset();
                if (!s.temporary.empty()) {
                    std::error_code ec;
                    std::filesystem::remove(s.temporary, ec);
                }
            }
        }
    } guard{sources};

    {
        SourceWim outer;
        auto file = DiskFile::open(wimPath);
        if (!file) {
            return std::unexpected(file.error());
        }
        outer.file = std::move(*file);
        auto table = readWimStreamTable(*outer.file);
        if (!table) {
            return std::unexpected(table.error());
        }
        if (table->header.totalParts != 1) {
            return fail(ErrorCode::Unsupported, L"a split WIM cannot be packed: join it first", wimPath.wstring());
        }
        outer.table = std::move(*table);
        outer.reader = std::make_unique<WimStreamReader>(*outer.file, outer.table);
        sources.push_back(std::move(outer));
    }
    auto rawHeader = readAll(*sources[0].file, 0, kWimHeaderSize);
    auto xml = readAll(*sources[0].file, sources[0].table.header.xmlOffset, sources[0].table.header.xmlSize);
    if (!rawHeader || !xml) {
        return std::unexpected(!rawHeader ? rawHeader.error() : xml.error());
    }

    Table out;
    out.xml = std::move(*xml);
    std::map<std::array<std::uint8_t, 20>, std::uint32_t> outerByHash;

    // 1. The source's streams: what each is (its first 4 KiB); WIMs inside it noted.
    std::vector<std::size_t> candidates;
    {
        const auto& entries = sources[0].table.entries;
        out.streams.resize(entries.size());
        for (const std::size_t i : byOffset(entries)) {
            if (auto cancelled = task.cancel.check(L"WLM"); !cancelled) {
                return std::unexpected(cancelled.error());
            }
            auto head = sniff(sources[0], entries[i]);
            if (!head) {
                return std::unexpected(head.error());
            }
            auto& rec = out.streams[i];
            rec.flags = entries[i].flags;
            rec.group = classify(*head, entries[i].metadata());
            rec.part = entries[i].part;
            rec.refCount = entries[i].refCount;
            rec.size = entries[i].original;
            rec.hash = entries[i].hash;
            outerByHash.emplace(rec.hash, static_cast<std::uint32_t>(i));
            if (options.openNested && !entries[i].metadata() && entries[i].original >= wlmNestedMinimum && head->size() >= 8 &&
                std::memcmp(head->data(), "MSWIM\0\0\0", 8) == 0) {
                candidates.push_back(i);
            }
        }
    }
    // 2. A plain WIM inside (WinRE.wim): its streams join the groups, those the image has too are
    //    kept once; on unpack it is rebuilt (LZX by wimgapi) and the metadata points at it.
    for (const std::size_t i : candidates) {
        SourceWim inner;
        inner.temporary = wlmPath.wstring() + std::format(L".nested{}.tmp", sources.size());
        {
            std::ofstream tmp(inner.temporary, std::ios::binary | std::ios::trunc);
            auto copied = sources[0].reader->read(sources[0].table.entries[i], [&](std::span<const std::byte> d) {
                tmp.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
                return static_cast<bool>(tmp);
            });
            if (!copied) {
                return std::unexpected(copied.error());
            }
        }
        auto file = DiskFile::open(inner.temporary);
        std::optional<Error> refused;
        if (!file) {
            refused = file.error();
        } else if (auto table = readWimStreamTable(**file); !table) {
            refused = table.error();
        } else if (table->header.totalParts != 1) {
            refused = Error{ErrorCode::Unsupported, L"split", L"WLM"};
        } else {
            inner.file = std::move(*file);
            inner.table = std::move(*table);
        }
        if (refused) {
            std::error_code ec;
            std::filesystem::remove(inner.temporary, ec);
            log::info("wlm", std::format(L"stream {} is a WIM WLM does not open ({}): kept as it is", i, refused->message));
            continue;
        }
        inner.reader = std::make_unique<WimStreamReader>(*inner.file, inner.table);
        auto innerHeader = readAll(*inner.file, 0, kWimHeaderSize);
        auto innerXml = readAll(*inner.file, inner.table.header.xmlOffset, inner.table.header.xmlSize);
        if (!innerHeader || !innerXml) {
            return std::unexpected(!innerHeader ? innerHeader.error() : innerXml.error());
        }
        const auto k = static_cast<std::uint16_t>(sources.size());
        inner.first = out.streams.size();
        out.nested.push_back(NestedRec{static_cast<std::uint32_t>(i), std::move(*innerHeader), std::move(*innerXml)});
        out.streams[i].group = kNoGroup;
        out.streams[i].nested = k;
        out.streams.resize(out.streams.size() + inner.table.entries.size());
        for (const std::size_t j : byOffset(inner.table.entries)) {
            const auto& e = inner.table.entries[j];
            auto& rec = out.streams[inner.first + j];
            rec.flags = e.flags;
            rec.part = e.part;
            rec.refCount = e.refCount;
            rec.size = e.original;
            rec.hash = e.hash;
            rec.owner = k;
            if (const auto same = outerByHash.find(e.hash); same != outerByHash.end() && out.streams[same->second].nested == 0) {
                rec.group = kNoGroup;
                rec.dupOf = same->second;
                continue;
            }
            auto head = sniff(inner, e);
            if (!head) {
                return std::unexpected(head.error());
            }
            rec.group = classify(*head, e.metadata());
        }
        sources.push_back(std::move(inner));
    }
    std::uint64_t totalPlain = 0;
    for (const auto& s : out.streams) {
        totalPlain += s.group < kWlmGroups ? s.size : 0;
    }

    // 3. Each group as one LZMA2 stream (x86 branch filter for code), the reader feeding the encoder.
    std::ofstream file(wlmPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCode::IoError, L"cannot create the WLM file", wlmPath.wstring());
    }
    file.write(std::string(kHeaderSize, '\0').data(), kHeaderSize);
    std::uint64_t position = kHeaderSize;
    const std::uint32_t dict = std::uint32_t{std::clamp<std::uint32_t>(options.blockMiB, 1, 1536)} << 20;
    std::uint64_t readPlain = 0;
    for (int g = 0; g < kWlmGroups; ++g) {
        auto& group = out.groups[static_cast<std::size_t>(g)];
        group.filter = g == kX64 || g == kX86 ? kBranchX86 : kNone;
        // Members: the source's in file order, then each nested WIM's in its file order.
        std::vector<std::pair<std::size_t, std::size_t>> members; // (source, entry)
        for (std::size_t s = 0; s < sources.size(); ++s) {
            for (const std::size_t j : byOffset(sources[s].table.entries)) {
                if (out.streams[sources[s].first + j].group == g) {
                    members.emplace_back(s, j);
                }
            }
        }
        for (const auto& [s, j] : members) {
            auto& rec = out.streams[sources[s].first + j];
            rec.groupOffset = group.plain;
            group.plain += rec.size;
        }
        if (group.plain == 0) {
            continue;
        }
        CLzma2EncHandle enc = Lzma2Enc_Create(&g_Alloc, &g_BigAlloc);
        if (!enc) {
            return fail(ErrorCode::Unknown, L"out of memory (LZMA2 encoder)", L"WLM");
        }
        CLzma2EncProps props;
        setProps(props, group.plain, dict, options.level);
        if (Lzma2Enc_SetProps(enc, &props) != SZ_OK) {
            Lzma2Enc_Destroy(enc);
            return fail(ErrorCode::Unknown, L"LZMA2 settings refused", L"WLM");
        }
        Lzma2Enc_SetDataSize(enc, group.plain);
        Block block;
        block.fileOffset = position;
        block.plain = group.plain;
        block.prop = Lzma2Enc_WriteProperties(enc);
        Pipe pipe(64u << 20);
        PipeIn in{{pipeRead}, &pipe};
        std::uint64_t written = 0;
        FileOut sink{{fileWrite}, &file, &written};
        SRes encoded = SZ_OK;
        std::thread encoder([&] {
            encoded = Lzma2Enc_Encode2(enc, &sink.vt, nullptr, nullptr, &in.vt, nullptr, 0, nullptr);
            if (encoded != SZ_OK) {
                pipe.abort();
            }
        });
        std::optional<Error> failure;
        BranchStream branch(true);
        auto push = [&](const char* p, std::size_t n) {
            if (n > 0 && !failure && !pipe.push(p, n)) {
                failure = Error{ErrorCode::Unknown, L"LZMA2 encoding failed", L"WLM"};
            }
        };
        for (const auto& [s, j] : members) {
            if (failure) {
                break;
            }
            auto copied = sources[s].reader->read(sources[s].table.entries[j], [&](std::span<const std::byte> d) {
                const char* p = reinterpret_cast<const char*>(d.data());
                if (group.filter == kBranchX86) {
                    branch.feed(p, d.size(), push);
                } else {
                    push(p, d.size());
                }
                readPlain += d.size();
                task.report(totalPlain ? static_cast<double>(readPlain) / static_cast<double>(totalPlain) : 1.0, L"WLM");
                return !failure && !task.cancel.cancelled();
            });
            if (!copied && !failure) {
                failure = task.cancel.cancelled() ? Error{ErrorCode::Cancelled, L"cancelled", L"WLM"} : copied.error();
            }
        }
        if (!failure && group.filter == kBranchX86) {
            branch.finish(push);
        }
        if (failure) {
            pipe.abort();
        } else {
            pipe.close();
        }
        encoder.join();
        Lzma2Enc_Destroy(enc);
        if (!failure && encoded != SZ_OK) {
            failure = Error{ErrorCode::Unknown, std::format(L"LZMA2 encoding failed ({})", encoded), L"WLM"};
        }
        if (failure) {
            file.close();
            std::error_code ec;
            std::filesystem::remove(wlmPath, ec);
            return std::unexpected(*failure);
        }
        block.packed = written;
        position += written;
        group.blocks.push_back(block);
        log::info("wlm", std::format(L"group {}: {} -> {} bytes", wlmGroupName(g), group.plain, written));
    }

    // 4. The table at the end, the header in front.
    const std::string plainTable = serialize(out);
    std::uint8_t tableProp = 0;
    auto packedTable = lzma2Encode(plainTable.data(), plainTable.size(), tableProp);
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
    report.threads = 2;
    report.seconds = secondsSince(started);
    log::info("wlm", std::format(L"packed {} -> {}: {} streams ({} nested WIM), {} -> {} bytes, {:.0f} s", wimPath.wstring(),
                                 wlmPath.wstring(), report.streams, report.nested, report.plainBytes, report.fileBytes,
                                 report.seconds));
    return report;
}

// ---- info / unpack -----------------------------------------------------------------------------------

Result<WimFile> readWlmWim(const std::filesystem::path& wlmPath) {
    std::ifstream in(wlmPath, std::ios::binary);
    std::uint64_t size = 0;
    auto opened = openWlm(in, wlmPath, size);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    const auto& [header, table] = *opened;
    WimFile wim;
    auto parsedHeader = readWimHeader(MemorySource(header.wimHeader, std::uint64_t{1} << 62));
    if (!parsedHeader) {
        return std::unexpected(parsedHeader.error());
    }
    wim.header = *parsedHeader;
    std::u16string xml(table.xml.size() / 2, u'\0');
    std::memcpy(xml.data(), table.xml.data(), xml.size() * 2);
    auto images = parseWimXml(xml);
    if (!images) {
        return std::unexpected(images.error());
    }
    wim.images = std::move(*images);
    return wim;
}

Result<WlmReport> readWlmInfo(const std::filesystem::path& wlmPath) {
    std::ifstream in(wlmPath, std::ios::binary);
    std::uint64_t size = 0;
    auto opened = openWlm(in, wlmPath, size);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    return reportOf(opened->second, size);
}

Result<WlmReport> unpackWlm(const std::filesystem::path& wlmPath, const std::filesystem::path& wimPath,
                            const TaskContext& task, bool lzxNested) {
    const auto started = std::chrono::steady_clock::now();
    std::ifstream in(wlmPath, std::ios::binary);
    std::uint64_t fileSize = 0;
    auto opened = openWlm(in, wlmPath, fileSize);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    const Header& header = opened->first;
    Table& table = opened->second;

    // Every stream sits in its group, back to back.
    std::array<std::vector<std::size_t>, kWlmGroups> members;
    for (std::size_t i = 0; i < table.streams.size(); ++i) {
        if (table.streams[i].group < kWlmGroups) {
            members[table.streams[i].group].push_back(i);
        }
    }
    for (std::size_t g = 0; g < kWlmGroups; ++g) {
        std::ranges::sort(members[g], {}, [&](std::size_t i) { return table.streams[i].groupOffset; });
        std::uint64_t next = 0;
        for (const std::size_t i : members[g]) {
            if (table.streams[i].groupOffset != next) {
                return fail(ErrorCode::ParseError, L"the WLM stream table is inconsistent", wlmPath.wstring());
            }
            next += table.streams[i].size;
        }
        std::uint64_t blocks = 0;
        for (const auto& b : table.groups[g].blocks) {
            if (b.fileOffset < kHeaderSize || b.fileOffset > fileSize || b.packed > fileSize - b.fileOffset) {
                return fail(ErrorCode::ParseError, L"a WLM block lies outside the file (truncated?)", wlmPath.wstring());
            }
            blocks += b.plain;
        }
        if (next != table.groups[g].plain || blocks != next || table.groups[g].blocks.size() > 1) {
            return fail(ErrorCode::ParseError, L"the WLM group sizes do not add up", wlmPath.wstring());
        }
    }

    // Outputs: the WIM, and one temporary plain WIM per nested one.
    std::vector<std::filesystem::path> files{wimPath};
    for (std::size_t k = 1; k <= table.nested.size(); ++k) {
        files.push_back(wimPath.wstring() + std::format(L".nested{}.tmp", k));
    }
    std::vector<std::fstream> outs(files.size());
    std::vector<std::uint64_t> ends(files.size(), kWimHeaderSize);
    std::error_code ec;
    auto removeAll = [&] {
        for (auto& o : outs) {
            o.close();
        }
        for (const auto& f : files) {
            std::filesystem::remove(f, ec);
        }
        for (std::size_t k = 1; k <= table.nested.size(); ++k) {
            std::filesystem::remove(wimPath.wstring() + std::format(L".nested{}.lzx", k), ec);
        }
    };
    for (std::size_t f = 0; f < files.size(); ++f) {
        outs[f].open(files[f], std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        if (!outs[f]) {
            removeAll();
            return fail(ErrorCode::IoError, L"cannot create the WIM", files[f].wstring());
        }
        outs[f].write(std::string(kWimHeaderSize, '\0').data(), kWimHeaderSize);
    }

    Sha1 sha;
    if (!sha.usable()) {
        removeAll();
        return fail(ErrorCode::Unknown, L"SHA-1 is not available", L"WLM");
    }
    std::vector<Placed> placed(table.streams.size());
    std::optional<Error> failure;
    std::uint64_t written = 0;
    std::vector<char> packed(kChunk);
    std::vector<char> plain(kChunk);
    for (int g = 0; g < kWlmGroups && !failure; ++g) {
        const auto& group = table.groups[static_cast<std::size_t>(g)];
        if (group.blocks.empty()) {
            continue;
        }
        std::size_t member = 0;
        std::uint64_t inStream = 0;
        auto route = [&](const char* p, std::size_t n) {
            for (;;) {
                if (failure || member >= members[g].size()) {
                    return;
                }
                const std::size_t i = members[g][member];
                const auto& s = table.streams[i];
                if (inStream == 0) {
                    placed[i] = Placed{ends[s.owner], s.size, s.hash};
                }
                const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(n, s.size - inStream));
                if (take > 0) {
                    outs[s.owner].write(p, static_cast<std::streamsize>(take));
                    sha.update(p, take);
                    ends[s.owner] += take;
                    inStream += take;
                    p += take;
                    n -= take;
                }
                if (inStream == s.size) {
                    if (sha.finish() != s.hash) {
                        failure = Error{ErrorCode::ParseError, L"a stream does not match its SHA-1 after unpacking",
                                        std::format(L"stream {}", i)};
                        return;
                    }
                    ++member;
                    inStream = 0;
                    continue; // the next one may be empty
                }
                if (n == 0) {
                    return;
                }
            }
        };
        BranchStream branch(false);
        auto deliver = [&](const char* p, std::size_t n) {
            if (group.filter == kBranchX86) {
                branch.feed(p, n, route);
            } else {
                route(p, n);
            }
            written += n;
            task.report(header.plainBytes ? 0.85 * static_cast<double>(written) / static_cast<double>(header.plainBytes) : 0.85,
                        L"WLM");
        };
        const Block& b = group.blocks.front();
        CLzma2Dec dec;
        Lzma2Dec_CONSTRUCT(&dec);
        if (Lzma2Dec_Allocate(&dec, b.prop, &g_Alloc) != SZ_OK) {
            failure = Error{ErrorCode::Unknown, L"out of memory (LZMA2 decoder)", L"WLM"};
            break;
        }
        Lzma2Dec_Init(&dec);
        std::uint64_t left = b.packed;
        std::uint64_t produced = 0;
        in.seekg(static_cast<std::streamoff>(b.fileOffset));
        std::size_t have = 0;
        std::size_t at = 0;
        while (!failure && produced < b.plain) {
            if (task.cancel.cancelled()) {
                failure = Error{ErrorCode::Cancelled, L"cancelled", L"WLM"};
                break;
            }
            if (at == have && left > 0) {
                have = static_cast<std::size_t>(std::min<std::uint64_t>(left, packed.size()));
                in.read(packed.data(), static_cast<std::streamsize>(have));
                if (!in) {
                    failure = Error{ErrorCode::IoError, L"a WLM block cannot be read", wlmPath.wstring()};
                    break;
                }
                left -= have;
                at = 0;
            }
            SizeT outLen = static_cast<SizeT>(std::min<std::uint64_t>(plain.size(), b.plain - produced));
            SizeT inLen = have - at;
            ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
            const SRes rc = Lzma2Dec_DecodeToBuf(&dec, reinterpret_cast<Byte*>(plain.data()), &outLen,
                                                 reinterpret_cast<const Byte*>(packed.data() + at), &inLen, LZMA_FINISH_ANY, &status);
            at += inLen;
            if (rc != SZ_OK || (outLen == 0 && inLen == 0)) {
                failure = Error{ErrorCode::ParseError, L"a WLM block does not decode", std::format(L"group {}", g)};
                break;
            }
            produced += outLen;
            deliver(plain.data(), outLen);
        }
        Lzma2Dec_Free(&dec, &g_Alloc);
        if (!failure && group.filter == kBranchX86) {
            branch.finish(route);
        }
        if (!failure && member != members[g].size()) {
            failure = Error{ErrorCode::ParseError, L"the WLM group ended before its streams did", wlmPath.wstring()};
        }
    }
    if (failure) {
        removeAll();
        return std::unexpected(*failure);
    }

    // Streams a nested WIM shares with the image: copied over from the WIM written so far.
    {
        std::vector<char> buffer(kChunk);
        for (std::size_t i = 0; i < table.streams.size(); ++i) {
            const auto& s = table.streams[i];
            if (s.dupOf == kNoStream) {
                continue;
            }
            const Placed from = placed[s.dupOf];
            placed[i] = Placed{ends[s.owner], s.size, s.hash};
            outs[0].flush();
            for (std::uint64_t done = 0; done < s.size;) {
                const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), s.size - done));
                outs[0].seekg(static_cast<std::streamoff>(from.offset + done));
                outs[0].read(buffer.data(), static_cast<std::streamsize>(n));
                outs[s.owner].seekp(static_cast<std::streamoff>(ends[s.owner]));
                outs[s.owner].write(buffer.data(), static_cast<std::streamsize>(n));
                ends[s.owner] += n;
                done += n;
            }
            outs[0].seekp(static_cast<std::streamoff>(ends[0]));
            if (!outs[0] || !outs[s.owner]) {
                removeAll();
                return fail(ErrorCode::IoError, L"a shared stream could not be copied", wimPath.wstring());
            }
        }
    }

    // Nested WIMs: finished as plain WIMs, then LZX by wimgapi (boot index kept), then into the image.
    std::vector<std::pair<std::array<std::uint8_t, 20>, std::array<std::uint8_t, 20>>> renamed;
    for (std::size_t k = 1; k <= table.nested.size(); ++k) {
        const NestedRec& n = table.nested[k - 1];
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < table.streams.size(); ++i) {
            if (table.streams[i].owner == k) {
                order.push_back(i);
            }
        }
        if (auto tail = writeWimTail(outs[k], ends[k], order, table, placed, n.wimHeader, n.xml); !tail) {
            removeAll();
            return std::unexpected(tail.error());
        }
        outs[k].close();
        std::filesystem::path lzx = wimPath.wstring() + std::format(L".nested{}.lzx", k);
        if (lzxNested) {
            const std::uint32_t images = get<std::uint32_t>(n.wimHeader, 44);
            for (std::uint32_t index = 1; index <= images; ++index) {
                if (auto r = exportImage(files[k], static_cast<int>(index), lzx, WimCompression::Lzx, TaskContext{task.cancel, {}}); !r) {
                    removeAll();
                    return std::unexpected(r.error());
                }
            }
            if (const std::uint32_t boot = get<std::uint32_t>(n.wimHeader, 120); boot > 0) {
                if (auto r = setBootImage(lzx, static_cast<int>(boot)); !r) {
                    removeAll();
                    return std::unexpected(r.error());
                }
            }
        } else {
            lzx = files[k]; // the plain rebuild goes in as it is
        }
        // Into the image as the stream it was, with its new SHA-1.
        std::ifstream rebuilt(lzx, std::ios::binary);
        const StreamRec& outer = table.streams[n.outerStream];
        Placed& p = placed[n.outerStream];
        p.offset = ends[0];
        p.size = 0;
        std::vector<char> buffer(kChunk);
        outs[0].seekp(static_cast<std::streamoff>(ends[0]));
        while (rebuilt) {
            rebuilt.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = static_cast<std::size_t>(rebuilt.gcount());
            if (got == 0) {
                break;
            }
            outs[0].write(buffer.data(), static_cast<std::streamsize>(got));
            sha.update(buffer.data(), got);
            p.size += got;
        }
        rebuilt.close();
        ends[0] += p.size;
        p.hash = sha.finish();
        renamed.emplace_back(outer.hash, p.hash);
        log::info("wlm", std::format(L"nested WIM {} rebuilt: {} bytes (was {})", k, p.size, outer.size));
        std::filesystem::remove(files[k], ec);
        std::filesystem::remove(lzx, ec);
        task.report(0.85 + 0.1 * static_cast<double>(k) / static_cast<double>(table.nested.size()), L"WLM");
    }

    // The editions' metadata name streams by SHA-1: a rebuilt nested WIM's new one goes in.
    if (!renamed.empty()) {
        for (std::size_t i = 0; i < table.streams.size(); ++i) {
            const auto& s = table.streams[i];
            if (s.owner != 0 || !(s.flags & 0x02)) {
                continue;
            }
            std::string meta(static_cast<std::size_t>(placed[i].size), '\0');
            outs[0].flush();
            outs[0].seekg(static_cast<std::streamoff>(placed[i].offset));
            outs[0].read(meta.data(), static_cast<std::streamsize>(meta.size()));
            bool changed = false;
            for (const auto& [from, to] : renamed) {
                const std::string_view needle(reinterpret_cast<const char*>(from.data()), from.size());
                for (std::size_t at = meta.find(needle); at != std::string::npos; at = meta.find(needle, at + needle.size())) {
                    std::memcpy(meta.data() + at, to.data(), to.size());
                    changed = true;
                }
            }
            if (changed) {
                outs[0].seekp(static_cast<std::streamoff>(placed[i].offset));
                outs[0].write(meta.data(), static_cast<std::streamsize>(meta.size()));
                sha.update(meta.data(), meta.size());
                placed[i].hash = sha.finish();
            }
        }
    }

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < table.streams.size(); ++i) {
        if (table.streams[i].owner == 0) {
            order.push_back(i);
        }
    }
    if (auto tail = writeWimTail(outs[0], ends[0], order, table, placed, header.wimHeader, table.xml); !tail) {
        removeAll();
        return std::unexpected(tail.error());
    }
    outs[0].close();
    for (std::size_t f = 1; f < files.size(); ++f) {
        std::filesystem::remove(files[f], ec);
    }
    WlmReport report = reportOf(table, fileSize);
    report.threads = 1;
    report.seconds = secondsSince(started);
    task.report(1.0, L"WLM");
    log::info("wlm", std::format(L"unpacked {} -> {}: {} streams checked, {} nested WIM rebuilt, {:.0f} s", wlmPath.wstring(),
                                 wimPath.wstring(), report.streams, table.nested.size(), report.seconds));
    return report;
}

} // namespace wl::core
