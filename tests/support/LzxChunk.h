#pragma once
// A tiny LZX *encoder* for the tests of the decoder (src/core/image/wim/Lzx.h): one verbatim
// block whose code is fixed — every literal 9 bits, and two match symbols of 2 bits:
//   Repeat3  the most recent offset again, 3 bytes   (position slot 0, length header 1)
//   Near2    offset 2 or 3, 2 bytes                  (position slot 4 + one extra bit)
// Enough to exercise the block header, the pretree-coded lengths, both decode paths of a tree,
// literal and match copies and the recent-offset queue without shipping a Windows file.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wl::test {

struct LzxToken {
    enum class Kind : std::uint8_t { Literal, Repeat3, Near2 };
    Kind kind = Kind::Literal;
    std::uint8_t value = 0; // Literal: the byte; Near2: 0 → offset 2, 1 → offset 3

    static LzxToken literal(std::uint8_t byte) { return {Kind::Literal, byte}; }
    static LzxToken repeat3() { return {Kind::Repeat3, 0}; }
    static LzxToken near2(int offset) { return {Kind::Near2, static_cast<std::uint8_t>(offset - 2)}; }
};

// Bits as LZX stores them: most significant first, in 16-bit little-endian words.
class LzxBitWriter {
public:
    void put(std::uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            m_word = static_cast<std::uint16_t>((m_word << 1) | ((value >> i) & 1u));
            if (++m_count == 16) {
                flush();
            }
        }
    }
    void alignToWord() {
        if (m_count > 0) {
            m_word = static_cast<std::uint16_t>(m_word << (16 - m_count));
            flush();
        }
    }
    void raw(std::uint8_t byte) { m_bytes.push_back(static_cast<std::byte>(byte)); }
    [[nodiscard]] std::vector<std::byte> finish() {
        alignToWord();
        return m_bytes;
    }

private:
    void flush() {
        m_bytes.push_back(static_cast<std::byte>(m_word & 0xFF));
        m_bytes.push_back(static_cast<std::byte>(m_word >> 8));
        m_word = 0;
        m_count = 0;
    }
    std::vector<std::byte> m_bytes;
    std::uint16_t m_word = 0;
    int m_count = 0;
};

// What the tokens decode to (before the decoder's x86 CALL translation).
inline std::vector<std::byte> lzxPlain(const std::vector<LzxToken>& tokens) {
    std::vector<std::byte> out;
    std::size_t recent = 1;
    for (const auto& token : tokens) {
        if (token.kind == LzxToken::Kind::Literal) {
            out.push_back(static_cast<std::byte>(token.value));
            continue;
        }
        const std::size_t length = token.kind == LzxToken::Kind::Repeat3 ? 3 : 2;
        if (token.kind == LzxToken::Kind::Near2) {
            recent = 2u + token.value;
        }
        for (std::size_t i = 0; i < length; ++i) {
            out.push_back(out[out.size() - recent]);
        }
    }
    return out;
}

// The tokens as one WIM LZX chunk whose block claims `blockSize` bytes.
inline std::vector<std::byte> lzxChunk(const std::vector<LzxToken>& tokens, std::size_t blockSize) {
    constexpr int kRepeat3 = 256 + ((0 << 3) | 1); // slot 0, length 3
    constexpr int kNear2 = 256 + ((4 << 3) | 0);   // slot 4, length 2
    LzxBitWriter bits;
    bits.put(1, 3); // verbatim block
    bits.put(0, 1); // its size follows
    bits.put(static_cast<std::uint32_t>(blockSize), 16);

    // A pretree of two 1-bit codes: symbol `zero` = "length unchanged", symbol `other` = the delta.
    auto pretree = [&](int other) {
        for (int symbol = 0; symbol < 20; ++symbol) {
            bits.put(symbol == 0 || symbol == other ? 1 : 0, 4);
        }
    };
    // Literals: length 9 from 0 → delta symbol (0 - 9) mod 17 = 8.
    pretree(8);
    for (int i = 0; i < 256; ++i) {
        bits.put(1, 1);
    }
    // Match symbols: length 2 → delta symbol 15; every other one stays 0.
    pretree(15);
    for (int symbol = 256; symbol < 496; ++symbol) {
        bits.put(symbol == kRepeat3 || symbol == kNear2 ? 1 : 0, 1);
    }
    // No length tree: all 249 lengths stay 0.
    pretree(1);
    for (int i = 0; i < 249; ++i) {
        bits.put(0, 1);
    }
    // Canonical codes: the two 2-bit symbols are 00 and 01, literal b is 1'bbbbbbbb.
    for (const auto& token : tokens) {
        switch (token.kind) {
        case LzxToken::Kind::Literal: bits.put(256u + token.value, 9); break;
        case LzxToken::Kind::Repeat3: bits.put(0, 2); break;
        case LzxToken::Kind::Near2:
            bits.put(1, 2);
            bits.put(token.value, 1); // the slot's one extra bit
            break;
        }
    }
    return bits.finish();
}

inline std::vector<std::byte> lzxChunk(const std::vector<LzxToken>& tokens) {
    return lzxChunk(tokens, lzxPlain(tokens).size());
}

inline std::vector<LzxToken> lzxLiterals(const std::vector<std::uint8_t>& bytes) {
    std::vector<LzxToken> tokens;
    for (const auto byte : bytes) {
        tokens.push_back(LzxToken::literal(byte));
    }
    return tokens;
}

} // namespace wl::test
