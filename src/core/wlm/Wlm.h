#pragma once
// WLM — WinLove Method (D-057): a WIM's streams regrouped by content and compressed harder than
// Microsoft's ESD, and turned back into a WIM with every stream's SHA-1 checked.
//
// File v2 (little endian):
//   0     header, 512 bytes: "WLM1" 1A 0A 00 00 · u32 header size · u32 version (2) · u64 table offset ·
//         u64 table packed size · u64 table plain size · u8 table LZMA2 property · … · u64 plain bytes (48) ·
//         the source WIM's own 208-byte header at 64
//   512   one LZMA2 stream per group (x86 branch filter first for code): the group's streams back to
//         back — the WIM's in its file order, then those of a WIM inside it (WinRE.wim)
//   end   table, LZMA2: groups (filter, plain size, the stream's offset / sizes / property) · streams:
//         the WIM's in lookup order, then each nested WIM's (flags, group, part, refcount, size,
//         offset in group, SHA-1, owner, nested, duplicate-of) · the XML · nested WIMs (which stream,
//         their 208-byte header, their XML)
// Groups: 0 x64 / arm64 code, 1 x86 code, 2 resource-only PE files (MUI), 3 everything else (metadata
// included). A nested WIM's streams the image has too are kept once (duplicate-of).
//
// Unpacking writes an uncompressed WIM (a standard file DISM mounts; wimgapi exports it to LZX).
// A nested WIM is rebuilt — plain, then LZX by wimgapi with its boot index — and since its bytes
// (so its SHA-1) differ from the original, the editions' metadata is pointed at the new SHA-1.
// Not kept: an integrity table, split parts, LZMS/solid sources (convert an ESD to WIM first).
#include "base/Result.h"
#include "core/image/WimFile.h"
#include "core/tasks/Task.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

inline constexpr int kWlmGroups = 4;

struct WlmPackOptions {
    std::uint32_t blockMiB = 1024; // LZMA2 dictionary (sliding window over each group); ~12 GB of memory at 1024
    unsigned threads = 0;          // unused since v2 (one encoder, its match finder on a second thread)
    int level = 9;
    bool openNested = true;        // WinRE.wim and other WIMs inside: their streams go in, deduplicated
};

struct WlmGroupStats {
    std::uint64_t streams = 0;
    std::uint64_t plain = 0;
    std::uint64_t packed = 0;
    std::uint32_t blocks = 0;
};

struct WlmReport {
    std::uint64_t streams = 0;
    std::uint64_t plainBytes = 0;
    std::uint64_t fileBytes = 0; // the whole output file
    std::array<WlmGroupStats, kWlmGroups> groups{};
    std::size_t nested = 0;       // WIMs inside that were opened
    std::uint64_t duplicates = 0; // their streams kept once (the image has them)
    unsigned threads = 0;
    double seconds = 0;
};

[[nodiscard]] const wchar_t* wlmGroupName(int group) noexcept; // "x64 code" …

[[nodiscard]] Result<WlmReport> packWlm(const std::filesystem::path& wim, const std::filesystem::path& wlm,
                                        const WlmPackOptions& options, const TaskContext& task);
// `lzxNested`: rebuild nested WIMs as LZX (wimgapi), as they were; false keeps them plain (tests).
[[nodiscard]] Result<WlmReport> unpackWlm(const std::filesystem::path& wlm, const std::filesystem::path& wim,
                                          const TaskContext& task, bool lzxNested = true);
// The source WIM's header and editions (from the XML the table keeps), for the Source / Images pages.
[[nodiscard]] Result<WimFile> readWlmWim(const std::filesystem::path& wlm);
// Header + table only (no stream is decoded).
[[nodiscard]] Result<WlmReport> readWlmInfo(const std::filesystem::path& wlm);
[[nodiscard]] bool isWlmFile(const std::filesystem::path& file);

// A WIM inside smaller than this is packed as an ordinary stream (tests lower it).
inline std::uint64_t wlmNestedMinimum = 16ull << 20;

} // namespace wl::core
