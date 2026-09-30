#pragma once
// LZX as WIM files use it (the default compression of install.wim): every 32 KiB chunk is
// compressed on its own — 32 KiB window, trees and recent offsets start fresh, no stream header —
// and x86 CALL targets are always translated with a fixed "file size" of 12 000 000.
// Decoder only: WinLove reads the streams to check them against their SHA-1 (WimVerify); writing
// WIMs stays with wimgapi.
#include <cstddef>
#include <span>

namespace wl::core {

// Decompresses one chunk into `out`, whose size is the chunk's uncompressed size (at most
// 32768). False when the data is not a valid chunk of that size.
[[nodiscard]] bool lzxDecompressChunk(std::span<const std::byte> in, std::span<std::byte> out) noexcept;

} // namespace wl::core
