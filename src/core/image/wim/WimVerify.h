#pragma once
// Reads every stream of a WIM and checks it against the SHA-1 its lookup table records — what
// tells a damaged download or a broken rewrite from a sound image before an ISO is built from it.
// wimgapi has no such pass (its "verify" flags only look at an integrity table, which Microsoft's
// images do not carry), so this is ours: lookup table, chunk tables, LZX (Lzx.h) / XPRESS (ntdll)
// and BCrypt. Works on any ByteSource: install.wim is checked in place inside an ISO.
// No admin, nothing is written. Not for ESD (LZMS, solid resources).
#include "core/image/WimFile.h"
#include "core/io/ByteSource.h"
#include "core/tasks/Task.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wl::core {

struct WimVerifyReport {
    struct Damage {
        std::uint64_t offset = 0; // of the stream in the file
        std::uint64_t size = 0;   // as stored
        bool metadata = false;    // an edition's file list, not a file's content
        std::wstring reason;
    };
    static constexpr std::size_t kMaxListed = 50;

    std::uint64_t streams = 0; // read and hashed
    std::uint64_t bytes = 0;   // their uncompressed size
    std::uint64_t damaged = 0;
    std::vector<Damage> first; // at most kMaxListed of them, by offset

    [[nodiscard]] bool sound() const noexcept { return damaged == 0; }
};

// An error only when the file cannot be read as a WIM at all (or is an ESD, or the task was
// cancelled); a damaged image is a report with `damaged` > 0. Progress is by stored bytes.
[[nodiscard]] Result<WimVerifyReport> verifyWim(const ByteSource& wim, const TaskContext& task);

// ---- The stream table and one stream at a time (WLM, D-057) ---------------------------------------
// The lookup table as the file has it (free entries and other parts of a split image left out), in
// the file's own order — the order a rebuilt WIM must keep (metadata resources = editions in order).
struct WimStreamEntry {
    std::uint64_t offset = 0;   // in the file
    std::uint64_t size = 0;     // as stored
    std::uint64_t original = 0; // uncompressed
    std::uint8_t flags = 0;     // 0x02 metadata, 0x04 compressed
    std::uint16_t part = 1;
    std::uint32_t refCount = 0;
    std::array<std::uint8_t, 20> hash{};
    [[nodiscard]] bool metadata() const noexcept { return (flags & 0x02) != 0; }
};
struct WimStreamTable {
    WimHeader header;
    std::uint32_t chunkSize = 32768;
    std::vector<WimStreamEntry> entries;
};
// Same formats as verifyWim: a plain WIM (none / XPRESS / LZX, 32 KiB chunks), one part.
[[nodiscard]] Result<WimStreamTable> readWimStreamTable(const ByteSource& wim);

// Reads streams one by one; reuses its buffers and its SHA-1 object.
class WimStreamReader {
public:
    WimStreamReader(const ByteSource& wim, const WimStreamTable& table);
    ~WimStreamReader();
    WimStreamReader(const WimStreamReader&) = delete;
    WimStreamReader& operator=(const WimStreamReader&) = delete;
    // The stream uncompressed through `write`; read whole, its SHA-1 must match (else an error).
    // `maxBytes`: stop after that many bytes (no hash check) — enough to tell what the stream is.
    [[nodiscard]] Result<void> read(const WimStreamEntry& entry, const std::function<bool(std::span<const std::byte>)>& write,
                                    std::uint64_t maxBytes = UINT64_MAX);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Compression research (wlcli dump-streams): every stream of a WIM, uncompressed, in file order —
// the exact data any other compressor must hold to stand in for the WIM's own. `stream` is called
// once per stream before its bytes; `write` gets the bytes. Single thread; same formats as verifyWim.
struct WimStreamInfo {
    std::uint64_t size = 0; // uncompressed
    bool metadata = false;  // an edition's file list
    std::array<std::uint8_t, 20> hash{};
};
[[nodiscard]] Result<WimVerifyReport> dumpWimStreams(const ByteSource& wim,
                                                     const std::function<void(const WimStreamInfo&)>& stream,
                                                     const std::function<bool(std::span<const std::byte>)>& write,
                                                     const TaskContext& task);

} // namespace wl::core
