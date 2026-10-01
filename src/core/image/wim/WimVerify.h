#pragma once
// Reads every stream of a WIM and checks it against the SHA-1 its lookup table records — what
// tells a damaged download or a broken rewrite from a sound image before an ISO is built from it.
// wimgapi has no such pass (its "verify" flags only look at an integrity table, which Microsoft's
// images do not carry), so this is ours: lookup table, chunk tables, LZX (Lzx.h) / XPRESS (ntdll)
// and BCrypt. Works on any ByteSource: install.wim is checked in place inside an ISO.
// No admin, nothing is written. Not for ESD (LZMS, solid resources).
#include "core/io/ByteSource.h"
#include "core/tasks/Task.h"

#include <array>
#include <cstdint>
#include <functional>
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
