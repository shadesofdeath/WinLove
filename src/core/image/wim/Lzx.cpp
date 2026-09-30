#include "core/image/wim/Lzx.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace wl::core {

namespace {

constexpr std::size_t kWindow = 32768;
constexpr int kPositionSlots = 30; // for a 32 KiB window
constexpr int kMainSymbols = 256 + kPositionSlots * 8;
constexpr int kLengthSymbols = 249;
constexpr int kPreSymbols = 20;
constexpr int kAlignedSymbols = 8;
constexpr int kMaxCodeLength = 16;
constexpr int kMinMatch = 2;
constexpr std::int32_t kCallFileSize = 12000000;

enum BlockType : std::uint32_t { Verbatim = 1, Aligned = 2, Uncompressed = 3 };

// Position slot → first formatted offset and number of extra bits.
constexpr std::array<std::uint32_t, kPositionSlots> kSlotBase = [] {
    std::array<std::uint32_t, kPositionSlots> base{};
    std::uint32_t next = 0;
    for (int slot = 0; slot < kPositionSlots; ++slot) {
        base[static_cast<std::size_t>(slot)] = next;
        const int extra = slot < 2 ? 0 : std::min((slot >> 1) - 1, 17);
        next += 1u << extra;
    }
    return base;
}();
constexpr int slotExtraBits(int slot) noexcept {
    return slot < 2 ? 0 : std::min((slot >> 1) - 1, 17);
}

// 16-bit little-endian words, bits taken from the most significant end. Looking ahead past the
// end of the input yields zeros; consuming them is an overrun.
class BitReader {
public:
    explicit BitReader(std::span<const std::byte> in) noexcept
        : m_next(reinterpret_cast<const std::uint8_t*>(in.data())), m_end(m_next + in.size()) {}

    [[nodiscard]] std::uint32_t peek(int count) noexcept { // 1 … 16
        while (m_bits < count) {
            std::uint32_t word = 0;
            if (m_end - m_next >= 2) {
                word = static_cast<std::uint32_t>(m_next[0]) | (static_cast<std::uint32_t>(m_next[1]) << 8);
                m_next += 2;
            } else {
                m_next = m_end;
                ++m_padding;
            }
            m_buffer |= word << (16 - m_bits);
            m_bits += 16;
        }
        return m_buffer >> (32 - count);
    }
    void skip(int count) noexcept {
        m_buffer <<= count;
        m_bits -= count;
    }
    [[nodiscard]] std::uint32_t read(int count) noexcept { // 0 … 16
        if (count == 0) {
            return 0;
        }
        const std::uint32_t value = peek(count);
        skip(count);
        return value;
    }

    // An uncompressed block starts on a word boundary; when the stream already is on one, a
    // whole word is skipped. Returns the byte position from which raw bytes follow; null when
    // the input has already ended.
    [[nodiscard]] const std::uint8_t* alignForBytes() noexcept {
        (void)peek(1);
        if (m_padding > 0) {
            return nullptr;
        }
        skip(m_bits % 16 == 0 ? 16 : m_bits % 16);
        m_next -= m_bits / 8; // whole words that were read ahead
        m_bits = 0;
        m_buffer = 0;
        return m_next;
    }
    void resumeAt(const std::uint8_t* position) noexcept { m_next = position; }
    [[nodiscard]] const std::uint8_t* end() const noexcept { return m_end; }
    [[nodiscard]] bool overrun() const noexcept { return m_bits < 16 * m_padding; }

private:
    const std::uint8_t* m_next;
    const std::uint8_t* m_end;
    std::uint32_t m_buffer = 0; // valid bits are the top m_bits
    int m_bits = 0;
    int m_padding = 0; // zero words supplied after the end of the input
};

// Canonical Huffman decoder: a direct table for codes up to `TableBits`, the first-code method
// for longer ones.
template <int Symbols, int TableBits>
class Huffman {
public:
    // False for an over-subscribed code. An empty or incomplete code builds; decoding a bit
    // pattern no symbol has then fails.
    [[nodiscard]] bool build(const std::uint8_t* lengths) noexcept {
        std::array<std::uint16_t, kMaxCodeLength + 1> count{};
        for (int symbol = 0; symbol < Symbols; ++symbol) {
            ++count[lengths[symbol]];
        }
        count[0] = 0;
        std::uint32_t code = 0;
        std::uint16_t offset = 0;
        for (int length = 1; length <= kMaxCodeLength; ++length) {
            code <<= 1;
            m_first[static_cast<std::size_t>(length)] = code;
            m_count[static_cast<std::size_t>(length)] = count[static_cast<std::size_t>(length)];
            m_offset[static_cast<std::size_t>(length)] = offset;
            code += count[static_cast<std::size_t>(length)];
            offset = static_cast<std::uint16_t>(offset + count[static_cast<std::size_t>(length)]);
            if (code > (1u << length)) {
                return false;
            }
        }
        std::array<std::uint16_t, kMaxCodeLength + 1> next = m_offset;
        for (int symbol = 0; symbol < Symbols; ++symbol) {
            if (const int length = lengths[symbol]) {
                m_sorted[next[static_cast<std::size_t>(length)]++] = static_cast<std::uint16_t>(symbol);
            }
        }
        m_table.fill(0);
        for (int length = 1; length <= TableBits; ++length) {
            const auto l = static_cast<std::size_t>(length);
            for (std::uint32_t i = 0; i < m_count[l]; ++i) {
                const std::uint32_t start = (m_first[l] + i) << (TableBits - length);
                const auto entry = static_cast<std::uint16_t>((m_sorted[m_offset[l] + i] << 5) | length);
                std::fill_n(m_table.begin() + start, std::size_t{1} << (TableBits - length), entry);
            }
        }
        return true;
    }

    // The next symbol, or -1 when the bits are no code.
    [[nodiscard]] int decode(BitReader& bits) const noexcept {
        const std::uint32_t window = bits.peek(kMaxCodeLength);
        if (const std::uint16_t entry = m_table[window >> (kMaxCodeLength - TableBits)]) {
            bits.skip(entry & 31);
            return entry >> 5;
        }
        for (int length = TableBits + 1; length <= kMaxCodeLength; ++length) {
            const auto l = static_cast<std::size_t>(length);
            const std::uint32_t code = window >> (kMaxCodeLength - length);
            if (code >= m_first[l] && code - m_first[l] < m_count[l]) {
                bits.skip(length);
                return m_sorted[m_offset[l] + (code - m_first[l])];
            }
        }
        return -1;
    }

private:
    std::array<std::uint16_t, std::size_t{1} << TableBits> m_table{}; // symbol << 5 | length; 0 = not here
    std::array<std::uint32_t, kMaxCodeLength + 1> m_first{};
    std::array<std::uint16_t, kMaxCodeLength + 1> m_count{};
    std::array<std::uint16_t, kMaxCodeLength + 1> m_offset{};
    std::array<std::uint16_t, Symbols> m_sorted{};
};

// Code lengths [first, last) of a tree, sent as differences to the previous block's lengths and
// run-length coded through a 20-symbol "pretree".
bool readLengths(BitReader& bits, std::uint8_t* lengths, int first, int last) noexcept {
    std::array<std::uint8_t, kPreSymbols> preLengths{};
    for (auto& length : preLengths) {
        length = static_cast<std::uint8_t>(bits.read(4));
    }
    Huffman<kPreSymbols, 8> pre;
    if (!pre.build(preLengths.data())) {
        return false;
    }
    auto delta = [](std::uint8_t previous, int symbol) { return static_cast<std::uint8_t>((previous + 17 - symbol) % 17); };
    for (int i = first; i < last;) {
        const int symbol = pre.decode(bits);
        if (symbol < 0) {
            return false;
        }
        if (symbol <= 16) {
            lengths[i] = delta(lengths[i], symbol);
            ++i;
            continue;
        }
        int run = 0;
        std::uint8_t value = 0;
        if (symbol == 17) {
            run = 4 + static_cast<int>(bits.read(4));
        } else if (symbol == 18) {
            run = 20 + static_cast<int>(bits.read(5));
        } else {
            run = 4 + static_cast<int>(bits.read(1));
            const int next = pre.decode(bits);
            if (next < 0 || next > 16) {
                return false;
            }
            value = delta(lengths[i], next);
        }
        if (run > last - i) {
            return false;
        }
        std::fill_n(lengths + i, run, value);
        i += run;
    }
    return true;
}

// Undoes the compressor's x86 CALL preprocessing: E8 operands were made absolute.
void restoreCalls(std::span<std::byte> data) noexcept {
    if (data.size() <= 10) {
        return;
    }
    auto* bytes = reinterpret_cast<std::uint8_t*>(data.data());
    const auto last = static_cast<std::int32_t>(data.size()) - 10;
    for (std::int32_t i = 0; i < last; ++i) {
        if (bytes[i] != 0xE8) {
            continue;
        }
        std::int32_t absolute = 0;
        std::memcpy(&absolute, bytes + i + 1, 4);
        std::int32_t relative = 0;
        bool changed = false;
        if (absolute >= 0) {
            if (absolute < kCallFileSize) {
                relative = absolute - i;
                changed = true;
            }
        } else if (absolute >= -i) {
            relative = absolute + kCallFileSize;
            changed = true;
        }
        if (changed) {
            std::memcpy(bytes + i + 1, &relative, 4);
        }
        i += 4;
    }
}

} // namespace

bool lzxDecompressChunk(std::span<const std::byte> in, std::span<std::byte> out) noexcept {
    if (out.size() > kWindow) {
        return false;
    }
    auto* window = reinterpret_cast<std::uint8_t*>(out.data());
    const std::size_t size = out.size();
    BitReader bits(in);
    std::array<std::uint8_t, kMainSymbols> mainLengths{};
    std::array<std::uint8_t, kLengthSymbols> lengthLengths{};
    std::array<std::uint32_t, 3> recent{1, 1, 1};
    Huffman<kMainSymbols, 11> mainTree;
    Huffman<kLengthSymbols, 10> lengthTree;
    Huffman<kAlignedSymbols, 7> alignedTree;

    std::size_t position = 0;
    while (position < size) {
        const std::uint32_t type = bits.read(3);
        const std::size_t blockSize = bits.read(1) ? kWindow : bits.read(16);
        if (blockSize == 0 || blockSize > size - position) {
            return false;
        }
        const std::size_t blockEnd = position + blockSize;

        if (type == Uncompressed) {
            const std::uint8_t* raw = bits.alignForBytes();
            if (!raw || static_cast<std::size_t>(bits.end() - raw) < 12 + blockSize) {
                return false;
            }
            for (auto& offset : recent) {
                std::memcpy(&offset, raw, 4);
                raw += 4;
            }
            std::memcpy(window + position, raw, blockSize);
            raw += blockSize;
            if ((blockSize & 1) && raw < bits.end()) {
                ++raw; // padding to a whole word
            }
            bits.resumeAt(raw);
            position = blockEnd;
            continue;
        }
        if (type != Verbatim && type != Aligned) {
            return false;
        }
        if (type == Aligned) {
            std::array<std::uint8_t, kAlignedSymbols> alignedLengths{};
            for (auto& length : alignedLengths) {
                length = static_cast<std::uint8_t>(bits.read(3));
            }
            if (!alignedTree.build(alignedLengths.data())) {
                return false;
            }
        }
        if (!readLengths(bits, mainLengths.data(), 0, 256) || !readLengths(bits, mainLengths.data(), 256, kMainSymbols) ||
            !mainTree.build(mainLengths.data()) || !readLengths(bits, lengthLengths.data(), 0, kLengthSymbols) ||
            !lengthTree.build(lengthLengths.data())) {
            return false;
        }

        while (position < blockEnd) {
            int symbol = mainTree.decode(bits);
            if (symbol < 0) {
                return false;
            }
            if (symbol < 256) {
                window[position++] = static_cast<std::uint8_t>(symbol);
                continue;
            }
            symbol -= 256;
            std::size_t length = static_cast<std::size_t>(symbol & 7) + kMinMatch;
            if ((symbol & 7) == 7) {
                const int more = lengthTree.decode(bits);
                if (more < 0) {
                    return false;
                }
                length += static_cast<std::size_t>(more);
            }
            const int slot = symbol >> 3;
            std::uint32_t offset = 0;
            if (slot < 3) {
                offset = recent[static_cast<std::size_t>(slot)];
                recent[static_cast<std::size_t>(slot)] = recent[0];
                recent[0] = offset;
            } else {
                const int extra = slotExtraBits(slot);
                std::uint32_t formatted = kSlotBase[static_cast<std::size_t>(slot)];
                if (type == Aligned && extra >= 3) {
                    formatted += bits.read(extra - 3) << 3;
                    const int low = alignedTree.decode(bits);
                    if (low < 0) {
                        return false;
                    }
                    formatted += static_cast<std::uint32_t>(low);
                } else {
                    formatted += bits.read(extra);
                }
                offset = formatted - 2;
                recent[2] = recent[1];
                recent[1] = recent[0];
                recent[0] = offset;
            }
            if (offset == 0 || offset > position || length > size - position) {
                return false;
            }
            // Byte by byte: the match may overlap what it writes.
            const std::uint8_t* from = window + position - offset;
            for (std::size_t i = 0; i < length; ++i) {
                window[position + i] = from[i];
            }
            position += length;
        }
        if (position != blockEnd) {
            return false; // a match ran over the end of its block
        }
    }
    if (bits.overrun()) {
        return false;
    }
    restoreCalls(out);
    return true;
}

} // namespace wl::core
