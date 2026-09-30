// The LZX decoder and the WIM verifier (P02 "Doğrula") on data built here: chunks from the tests'
// own small encoder (support/LzxChunk.h) and a WIM assembled in memory. The real image — 94 409
// streams of the 25H2 install.wim, every SHA-1 matching — is checked with `wlcli verify`.
#include "core/image/wim/Lzx.h"
#include "core/image/wim/WimVerify.h"
#include "support/LzxChunk.h"

#include <doctest.h>

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <cstring>
#include <string_view>

using namespace wl;
using namespace wl::core;
using wl::test::LzxToken;

namespace {

std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::array<std::uint8_t, 20> sha1(std::span<const std::byte> data) {
    std::array<std::uint8_t, 20> digest{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    REQUIRE(BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0)));
    std::byte none{};
    REQUIRE(BCRYPT_SUCCESS(BCryptHash(algorithm, nullptr, 0,
                                      reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.empty() ? &none : data.data())),
                                      static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size()))));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return digest;
}

class MemorySource final : public ByteSource {
public:
    std::vector<std::byte> data;

    [[nodiscard]] std::uint64_t size() const override { return data.size(); }
    [[nodiscard]] Result<void> read(std::uint64_t offset, std::span<std::byte> out) const override {
        if (offset > data.size() || out.size() > data.size() - offset) {
            return fail(ErrorCode::IoError, L"read past end");
        }
        std::memcpy(out.data(), data.data() + offset, out.size());
        return {};
    }
};

// A WIM with the given streams, laid out as wimgapi does: header, streams, lookup table, XML.
struct Stream {
    std::vector<std::byte> stored;
    std::vector<std::byte> plain;
    bool compressed = false;
    bool metadata = false;
};
constexpr std::uint32_t kLzxFlags = 0x00000002 | 0x00040000;

MemorySource buildWim(const std::vector<Stream>& streams, std::uint32_t flags = kLzxFlags) {
    MemorySource wim;
    auto& out = wim.data;
    out.resize(208);
    auto put = [&](std::size_t at, auto value) { std::memcpy(out.data() + at, &value, sizeof(value)); };
    auto resource = [&](std::size_t at, std::uint64_t size, std::uint8_t resourceFlags, std::uint64_t offset,
                        std::uint64_t original) {
        put(at, size | (static_cast<std::uint64_t>(resourceFlags) << 56));
        put(at + 8, offset);
        put(at + 16, original);
    };
    std::memcpy(out.data(), "MSWIM\0\0\0", 8);
    put(8, std::uint32_t{208});
    put(12, std::uint32_t{0x00010D00});
    put(16, flags);
    put(20, std::uint32_t{32768});
    put(40, std::uint16_t{1});
    put(42, std::uint16_t{1});
    put(44, std::uint32_t{0});

    std::vector<std::byte> lookup;
    for (const auto& stream : streams) {
        std::array<std::byte, 50> entry{};
        const std::uint8_t entryFlags = static_cast<std::uint8_t>((stream.compressed ? 0x04 : 0) | (stream.metadata ? 0x02 : 0));
        const std::uint64_t packed = stream.stored.size() | (static_cast<std::uint64_t>(entryFlags) << 56);
        const std::uint64_t offset = out.size();
        const std::uint64_t original = stream.plain.size();
        std::memcpy(entry.data(), &packed, 8);
        std::memcpy(entry.data() + 8, &offset, 8);
        std::memcpy(entry.data() + 16, &original, 8);
        const std::uint16_t part = 1;
        std::memcpy(entry.data() + 24, &part, 2);
        const std::uint32_t references = 1;
        std::memcpy(entry.data() + 26, &references, 4);
        const auto digest = sha1(stream.plain);
        std::memcpy(entry.data() + 30, digest.data(), digest.size());
        lookup.insert(lookup.end(), entry.begin(), entry.end());
        out.insert(out.end(), stream.stored.begin(), stream.stored.end());
    }
    resource(48, lookup.size(), 0x02, out.size(), lookup.size());
    out.insert(out.end(), lookup.begin(), lookup.end());
    const std::u16string xml = u"﻿<WIM></WIM>";
    resource(72, xml.size() * 2, 0x02, out.size(), xml.size() * 2);
    const auto* xmlBytes = reinterpret_cast<const std::byte*>(xml.data());
    out.insert(out.end(), xmlBytes, xmlBytes + xml.size() * 2);
    return wim;
}

std::vector<LzxToken> sampleTokens() {
    std::vector<LzxToken> tokens = wl::test::lzxLiterals({'W', 'i', 'n', 'L', 'o', 'v', 'e', ' '});
    tokens.push_back(LzxToken::literal('a'));
    tokens.push_back(LzxToken::repeat3());  // "aaa" (the first recent offset is 1)
    tokens.push_back(LzxToken::literal('b'));
    tokens.push_back(LzxToken::near2(2));   // "ab"
    tokens.push_back(LzxToken::repeat3());  // offset 2 again: "aba"
    tokens.push_back(LzxToken::near2(3));   // two bytes from three back
    tokens.push_back(LzxToken::literal('!'));
    for (int i = 0; i < 200; ++i) {         // repetitive enough to come out smaller than it went in
        tokens.push_back(LzxToken::literal(static_cast<std::uint8_t>('0' + i % 10)));
        tokens.push_back(LzxToken::repeat3());
    }
    return tokens;
}

} // namespace

TEST_CASE("lzx: a verbatim block with literals, matches and recent offsets decodes") {
    const auto tokens = sampleTokens();
    const auto plain = wl::test::lzxPlain(tokens);
    const auto chunk = wl::test::lzxChunk(tokens);
    REQUIRE(plain.size() > 100);
    REQUIRE(chunk.size() < plain.size()); // a WIM stores a chunk that did not shrink as it is
    CHECK(std::string_view(reinterpret_cast<const char*>(plain.data()), 21) == "WinLove aaaabababaab!");

    std::vector<std::byte> out(plain.size());
    REQUIRE(lzxDecompressChunk(chunk, out));
    CHECK(out == plain);

    // Anything but exactly this chunk of exactly this size is refused.
    std::vector<std::byte> wrongSize(plain.size() + 1);
    CHECK_FALSE(lzxDecompressChunk(chunk, wrongSize));
    CHECK_FALSE(lzxDecompressChunk(std::span(chunk).first(chunk.size() / 2), out));
    CHECK_FALSE(lzxDecompressChunk({}, out));
    std::vector<std::byte> tooLarge(32769);
    CHECK_FALSE(lzxDecompressChunk(chunk, tooLarge));
}

TEST_CASE("lzx: an uncompressed block is copied; a match may not reach before the chunk") {
    const auto text = bytesOf("stored as it is: odd."); // 21 bytes: the padding byte is missing at the end
    wl::test::LzxBitWriter bits;
    bits.put(3, 3); // uncompressed block
    bits.put(0, 1);
    bits.put(static_cast<std::uint32_t>(text.size()), 16);
    bits.alignToWord();
    for (int i = 0; i < 3; ++i) { // the three recent offsets, little-endian
        bits.raw(1);
        bits.raw(0);
        bits.raw(0);
        bits.raw(0);
    }
    for (const auto byte : text) {
        bits.raw(static_cast<std::uint8_t>(byte));
    }
    const auto chunk = bits.finish();
    std::vector<std::byte> out(text.size());
    REQUIRE(lzxDecompressChunk(chunk, out));
    CHECK(out == text);

    // "Repeat the last offset" as the very first token: there is nothing one byte back.
    const auto bad = wl::test::lzxChunk({LzxToken::repeat3()}, 3);
    std::vector<std::byte> three(3);
    CHECK_FALSE(lzxDecompressChunk(bad, three));
}

TEST_CASE("lzx: x86 CALL operands are turned back into relative ones") {
    // E8 at position 2 with the absolute target 0x100 → relative 0x100 - 2; the E8 in the last
    // ten bytes is left alone.
    std::vector<std::uint8_t> stored{0x90, 0x90, 0xE8, 0x00, 0x01, 0x00, 0x00, 0x90, 0x90, 0x90,
                                     0x90, 0x90, 0x90, 0x90, 0xE8, 0x00, 0x01, 0x00, 0x00, 0x90};
    const auto chunk = wl::test::lzxChunk(wl::test::lzxLiterals(stored));
    std::vector<std::byte> out(stored.size());
    REQUIRE(lzxDecompressChunk(chunk, out));
    std::vector<std::uint8_t> expected = stored;
    expected[3] = 0xFE;
    expected[4] = 0x00;
    CHECK(std::memcmp(out.data(), expected.data(), expected.size()) == 0);
}

TEST_CASE("verify: a sound WIM, then the same one with a flipped bit, a bad size, a missing end") {
    const auto tokens = sampleTokens();
    Stream compressed{wl::test::lzxChunk(tokens), wl::test::lzxPlain(tokens), true, false};
    Stream plain{bytesOf("a file stored without compression"), bytesOf("a file stored without compression"), false, false};
    Stream metadata{bytesOf("the file list of an edition"), bytesOf("the file list of an edition"), false, true};
    Stream empty{};
    const MemorySource sound = buildWim({compressed, plain, metadata, empty});

    const auto report = verifyWim(sound, TaskContext{});
    REQUIRE(report.has_value());
    CHECK(report->sound());
    CHECK(report->streams == 4);
    CHECK(report->bytes == compressed.plain.size() + plain.plain.size() + metadata.plain.size());

    // One bit in the compressed stream, one in the metadata.
    MemorySource damaged = sound;
    damaged.data[208 + compressed.stored.size() - 8] ^= std::byte{0x10};
    const std::size_t metadataAt = 208 + compressed.stored.size() + plain.stored.size();
    damaged.data[metadataAt + 3] ^= std::byte{0x01};
    const auto found = verifyWim(damaged, TaskContext{});
    REQUIRE(found.has_value());
    CHECK_FALSE(found->sound());
    CHECK(found->damaged == 2);
    CHECK(found->streams == 4);
    REQUIRE(found->first.size() == 2);
    CHECK(found->first[0].offset == 208);
    CHECK_FALSE(found->first[0].metadata);
    CHECK(found->first[1].offset == metadataAt);
    CHECK(found->first[1].metadata);

    // A stream that claims to end after the file does (a cut-off download).
    Stream beyond = plain;
    MemorySource cut = buildWim({plain, beyond});
    const std::uint64_t huge = cut.data.size() * 2;
    const std::size_t lookupAt = 208 + 2 * plain.stored.size();
    std::memcpy(cut.data.data() + lookupAt + 50 + 8, &huge, 8); // second entry: offset
    const auto truncated = verifyWim(cut, TaskContext{});
    REQUIRE(truncated.has_value());
    CHECK(truncated->damaged == 1);
}

TEST_CASE("verify: what is not a plain WIM is refused, not reported as sound") {
    MemorySource notWim;
    notWim.data = bytesOf(std::string(400, 'x'));
    CHECK_FALSE(verifyWim(notWim, TaskContext{}).has_value());

    // An ESD: LZMS.
    const Stream plain{bytesOf("x"), bytesOf("x"), false, false};
    const MemorySource esd = buildWim({plain}, 0x00000002 | 0x00080000);
    const auto refused = verifyWim(esd, TaskContext{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::Unsupported);

    // Cancelled before it starts.
    const MemorySource sound = buildWim({plain});
    TaskContext task;
    task.cancel.cancel();
    const auto cancelled = verifyWim(sound, task);
    REQUIRE_FALSE(cancelled.has_value());
    CHECK(cancelled.error().code == ErrorCode::Cancelled);
}
