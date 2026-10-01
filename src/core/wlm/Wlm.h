#pragma once
// WLM — WinLove Method (D-057): a WIM's streams regrouped by content and compressed harder than
// Microsoft's ESD, and turned back into a WIM with every stream's SHA-1 checked.
//
// File (little endian):
//   0     header, 512 bytes: "WLM1" 1A 0A 00 00 · u32 header size · u32 version (1) · u64 table offset ·
//         u64 table packed size · u64 table plain size · u8 table LZMA2 property · … · u64 plain bytes (48) ·
//         the source WIM's own 208-byte header at 64
//   512   blocks: each group's streams back to back (lookup-table offset order), cut into blocks of
//         `blockSize` plain bytes, each block compressed on its own (x86 branch filter for code, then LZMA2)
//   end   table, LZMA2: groups (filter, plain size, blocks: file offset, packed, plain, LZMA2 property) ·
//         streams in the WIM's lookup order (flags, group, part, refcount, size, offset in group, SHA-1) ·
//         the XML resource as it was
// Groups: 0 x64 / arm64 code, 1 x86 code, 2 resource-only PE files (MUI), 3 everything else (metadata
// included). A stream belongs to one group; a block never spans two groups.
//
// Unpacking writes an uncompressed WIM (a standard file DISM mounts; wimgapi can export it to LZX):
// the groups' bytes back to back, then the lookup table in the original order, then the XML.
// Not kept: an integrity table, split parts, LZMS/solid sources (convert an ESD to WIM first).
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

inline constexpr int kWlmGroups = 4;

struct WlmPackOptions {
    std::uint32_t blockMiB = 1024; // plain bytes per block = the LZMA2 dictionary (64: ~3 % worse than ESD; 1024: 12 % better)
    unsigned threads = 0;         // blocks compressed at once; 0 = from free memory and cores
    int level = 9;
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
    unsigned threads = 0;
    double seconds = 0;
};

[[nodiscard]] const wchar_t* wlmGroupName(int group) noexcept; // "x64 code" …

[[nodiscard]] Result<WlmReport> packWlm(const std::filesystem::path& wim, const std::filesystem::path& wlm,
                                        const WlmPackOptions& options, const TaskContext& task);
[[nodiscard]] Result<WlmReport> unpackWlm(const std::filesystem::path& wlm, const std::filesystem::path& wim,
                                          const TaskContext& task, unsigned threads = 0);
// Header + table only (no block is decoded).
[[nodiscard]] Result<WlmReport> readWlmInfo(const std::filesystem::path& wlm);
[[nodiscard]] bool isWlmFile(const std::filesystem::path& file);

} // namespace wl::core
