// D-057 WLM: a small WIM written here (uncompressed, the layout ENGINE.md describes) goes through
// packWlm / unpackWlm; the result must be the same WIM in every respect a reader looks at.
#include "core/image/WimFile.h"
#include "core/image/wim/WimVerify.h"
#include "core/io/ByteSource.h"
#include "core/wlm/Wlm.h"

#include <doctest.h>

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"wlm";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string bytesOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::array<std::uint8_t, 20> sha1(const std::string& data) {
    std::array<std::uint8_t, 20> d{};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
    BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), d.data(),
               static_cast<ULONG>(d.size()));
    BCryptCloseAlgorithmProvider(alg, 0);
    return d;
}

template <class T>
void put(std::string& out, T v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}

// A PE header: machine, and one section that is code (or not).
std::string pe(std::uint16_t machine, bool code, std::size_t size, unsigned seed) {
    std::string b(size, '\0');
    std::mt19937 rng(seed);
    for (std::size_t i = 512; i < size; ++i) {
        b[i] = static_cast<char>(i % 7 == 0 ? 0xE8 : rng() % 32); // calls and small values: compressible, filter-relevant
    }
    b[0] = 'M';
    b[1] = 'Z';
    const std::uint32_t at = 0x80;
    std::memcpy(&b[0x3C], &at, 4);
    std::memcpy(&b[at], "PE\0\0", 4);
    std::memcpy(&b[at + 4], &machine, 2);
    const std::uint16_t sections = 1;
    std::memcpy(&b[at + 6], &sections, 2);
    const std::uint16_t optional = 0xF0;
    std::memcpy(&b[at + 20], &optional, 2);
    const std::uint32_t characteristics = code ? 0x60000020u : 0x40000040u;
    std::memcpy(&b[at + 24 + optional + 36], &characteristics, 4);
    return b;
}

struct Stream {
    std::string data;
    std::uint8_t flags = 0;
    std::uint32_t refCount = 1;
};

// An uncompressed one-edition WIM with `streams`; returns the file's bytes.
std::string makeWim(const std::vector<Stream>& streams, std::uint32_t bootIndex = 0) {
    std::string file(208, '\0');
    std::string lookup;
    std::string bootMetadata(24, '\0');
    std::uint32_t metadataSeen = 0;
    for (const auto& s : streams) {
        if ((s.flags & 0x02) && ++metadataSeen == bootIndex) {
            bootMetadata.clear();
            put<std::uint64_t>(bootMetadata, s.data.size() | (std::uint64_t{s.flags} << 56));
            put<std::uint64_t>(bootMetadata, file.size());
            put<std::uint64_t>(bootMetadata, s.data.size());
        }
        const std::uint64_t offset = file.size();
        file += s.data;
        put<std::uint64_t>(lookup, s.data.size() | (std::uint64_t{s.flags} << 56));
        put<std::uint64_t>(lookup, offset);
        put<std::uint64_t>(lookup, s.data.size());
        put<std::uint16_t>(lookup, 1);
        put<std::uint32_t>(lookup, s.refCount);
        const auto h = sha1(s.data);
        lookup.append(reinterpret_cast<const char*>(h.data()), h.size());
    }
    const std::uint64_t lookupOffset = file.size();
    file += lookup;
    const std::u16string xmlText = u"﻿<WIM><TOTALBYTES>0</TOTALBYTES><IMAGE INDEX=\"1\"><NAME>WLM test</NAME>"
                                   u"<WINDOWS><ARCH>9</ARCH></WINDOWS></IMAGE></WIM>";
    const std::string xml(reinterpret_cast<const char*>(xmlText.data()), xmlText.size() * 2);
    const std::uint64_t xmlOffset = file.size();
    file += xml;

    std::string h;
    h.append("MSWIM\0\0\0", 8);
    put<std::uint32_t>(h, 208);
    put<std::uint32_t>(h, 0x10D00);
    put<std::uint32_t>(h, 0x80); // reparse-point fix, no compression
    put<std::uint32_t>(h, 0);
    h.append("0123456789abcdef", 16);
    put<std::uint16_t>(h, 1);
    put<std::uint16_t>(h, 1);
    put<std::uint32_t>(h, 1);
    put<std::uint64_t>(h, lookup.size() | (std::uint64_t{0x02} << 56));
    put<std::uint64_t>(h, lookupOffset);
    put<std::uint64_t>(h, lookup.size());
    put<std::uint64_t>(h, xml.size());
    put<std::uint64_t>(h, xmlOffset);
    put<std::uint64_t>(h, xml.size());
    h += bootMetadata;
    put<std::uint32_t>(h, bootIndex);
    h.resize(208, '\0');
    std::memcpy(file.data(), h.data(), 208);
    return file;
}

std::vector<Stream> sample() {
    std::vector<Stream> s;
    s.push_back({pe(0x8664, true, (5u << 20) / 2, 1)});   // x64 code, 2.5 MiB: crosses 1 MiB blocks
    s.push_back({pe(0x14C, true, 300'000, 2)});            // x86 code
    s.push_back({pe(0x14C, false, 120'000, 3), 0, 3});     // resource-only (MUI), refcount 3
    std::string text;
    for (int i = 0; i < 20000; ++i) {
        text += "<assembly name=\"Microsoft-Windows-Something\" version=\"10.0.26100." + std::to_string(i % 97) + "\"/>\n";
    }
    s.push_back({text});
    s.push_back({std::string()});                          // an empty stream
    s.push_back({std::string(70'000, 'm'), 0x02});         // the edition's metadata
    return s;
}

} // namespace

TEST_CASE("wlm: pack and unpack give back the WIM, streams in their groups, SHA-1 checked") {
    const auto streams = sample();
    const auto wim = scratch(L"source.wim");
    std::ofstream(wim, std::ios::binary | std::ios::trunc) << makeWim(streams);

    const auto wlm = scratch(L"packed.wlm");
    WlmPackOptions options;
    options.blockMiB = 1;
    options.threads = 2;
    auto packed = packWlm(wim, wlm, options, TaskContext{});
    REQUIRE(packed);
    CHECK(isWlmFile(wlm));
    CHECK_FALSE(isWlmFile(wim));
    CHECK(packed->streams == streams.size());
    CHECK(packed->groups[0].streams == 1); // x64 code
    CHECK(packed->groups[0].blocks == 1);  // v2: one LZMA2 stream per group (1 MiB window over 2.5 MiB)
    CHECK(packed->groups[1].streams == 1); // x86 code
    CHECK(packed->groups[2].streams == 1); // resources
    CHECK(packed->groups[3].streams == 3); // text, empty, metadata
    CHECK(packed->fileBytes < std::filesystem::file_size(wim));

    auto info = readWlmInfo(wlm);
    REQUIRE(info);
    CHECK(info->plainBytes == packed->plainBytes);

    const auto back = scratch(L"back.wim");
    auto unpacked = unpackWlm(wlm, back, TaskContext{}, false);
    REQUIRE(unpacked);

    // The rebuilt WIM: same streams (bytes, flags, refcounts), same order, same XML, sound.
    auto file = DiskFile::open(back);
    REQUIRE(file);
    auto table = readWimStreamTable(**file);
    REQUIRE(table);
    CHECK(table->header.compression == WimCompression::None);
    CHECK(table->header.imageCount == 1);
    REQUIRE(table->entries.size() == streams.size());
    WimStreamReader reader(**file, *table);
    for (std::size_t i = 0; i < streams.size(); ++i) {
        const auto& e = table->entries[i];
        CHECK(e.original == streams[i].data.size());
        CHECK(e.refCount == streams[i].refCount);
        CHECK(e.metadata() == ((streams[i].flags & 0x02) != 0));
        CHECK(e.hash == sha1(streams[i].data));
        std::string got;
        REQUIRE(reader.read(e, [&](std::span<const std::byte> d) {
            got.append(reinterpret_cast<const char*>(d.data()), d.size());
            return true;
        }));
        CHECK(got == streams[i].data);
    }
    auto verified = verifyWim(**file, TaskContext{});
    REQUIRE(verified);
    CHECK(verified->sound());
    auto parsed = readWim(**file);
    REQUIRE(parsed);
    REQUIRE(parsed->images.size() == 1);
    CHECK(parsed->images[0].name == L"WLM test");
}

TEST_CASE("wlm: a damaged block or a foreign file is refused, nothing half-written stays") {
    const auto wim = scratch(L"source2.wim");
    std::ofstream(wim, std::ios::binary | std::ios::trunc) << makeWim(sample());
    const auto wlm = scratch(L"packed2.wlm");
    WlmPackOptions options;
    options.blockMiB = 1;
    options.threads = 1;
    REQUIRE(packWlm(wim, wlm, options, TaskContext{}));

    std::string bytes = bytesOf(wlm);
    bytes[600] = static_cast<char>(bytes[600] ^ 0x5A); // inside the first block
    const auto broken = scratch(L"broken.wlm");
    std::ofstream(broken, std::ios::binary | std::ios::trunc) << bytes;
    const auto out = scratch(L"broken.wim");
    CHECK_FALSE(unpackWlm(broken, out, TaskContext{}, false));
    CHECK_FALSE(std::filesystem::exists(out));

    CHECK_FALSE(readWlmInfo(wim));                                            // a WIM is not a WLM
    CHECK_FALSE(unpackWlm(scratch(L"missing.wlm"), out, TaskContext{}, false));   // nor is nothing
    bytes = bytesOf(wlm);
    bytes.resize(bytes.size() - 10);                                          // cut short: the table is gone
    std::ofstream(broken, std::ios::binary | std::ios::trunc) << bytes;
    CHECK_FALSE(unpackWlm(broken, out, TaskContext{}, false));
}

TEST_CASE("wlm: a WIM inside (WinRE) is opened, shared streams kept once, rebuilt, and the metadata follows it") {
    // The inner WIM: one stream the image has too, one of its own, its metadata; it boots index 1.
    const std::string shared = pe(0x8664, true, 300'000, 7);
    std::vector<Stream> innerStreams{{shared}, {std::string(90'000, 'r')}, {std::string(40'000, 'n'), 0x02}};
    const std::string inner = makeWim(innerStreams, 1);
    const auto innerHash = sha1(inner);
    // The image's metadata names the inner WIM by its SHA-1 (as a dentry does).
    std::string metadata(30'000, 'm');
    metadata.replace(1000, 20, reinterpret_cast<const char*>(innerHash.data()), 20);
    std::vector<Stream> outerStreams{{shared}, {inner}, {std::string(200'000, 't')}, {metadata, 0x02}};
    const auto wim = scratch(L"outer.wim");
    std::ofstream(wim, std::ios::binary | std::ios::trunc) << makeWim(outerStreams);

    const auto saved = wlmNestedMinimum;
    wlmNestedMinimum = 0;
    const auto wlm = scratch(L"outer.wlm");
    WlmPackOptions options;
    options.blockMiB = 1;
    auto packed = packWlm(wim, wlm, options, TaskContext{});
    wlmNestedMinimum = saved;
    REQUIRE(packed);
    CHECK(packed->nested == 1);
    CHECK(packed->duplicates == 1);

    const auto back = scratch(L"outer-back.wim");
    REQUIRE(unpackWlm(wlm, back, TaskContext{}, /*lzxNested=*/false));
    auto file = DiskFile::open(back);
    REQUIRE(file);
    auto table = readWimStreamTable(**file);
    REQUIRE(table);
    REQUIRE(table->entries.size() == outerStreams.size());
    WimStreamReader reader(**file, *table);
    auto bytes = [&](const WimStreamEntry& e) {
        std::string got;
        REQUIRE(reader.read(e, [&](std::span<const std::byte> d) {
            got.append(reinterpret_cast<const char*>(d.data()), d.size());
            return true;
        }));
        return got;
    };
    // Unchanged streams come back as they were.
    CHECK(bytes(table->entries[0]) == shared);
    CHECK(bytes(table->entries[2]) == outerStreams[2].data);
    // The inner WIM: rebuilt (other bytes, so another SHA-1), its streams and boot index intact.
    const std::string rebuilt = bytes(table->entries[1]);
    const auto rebuiltHash = sha1(rebuilt);
    CHECK(rebuiltHash != innerHash);
    CHECK(table->entries[1].hash == rebuiltHash);
    const auto rebuiltFile = scratch(L"rebuilt-inner.wim");
    std::ofstream(rebuiltFile, std::ios::binary | std::ios::trunc) << rebuilt;
    auto innerFile = DiskFile::open(rebuiltFile);
    REQUIRE(innerFile);
    auto innerTable = readWimStreamTable(**innerFile);
    REQUIRE(innerTable);
    CHECK(innerTable->header.bootIndex == 1);
    REQUIRE(innerTable->entries.size() == innerStreams.size());
    for (std::size_t i = 0; i < innerStreams.size(); ++i) {
        CHECK(innerTable->entries[i].hash == sha1(innerStreams[i].data));
    }
    CHECK(verifyWim(**innerFile, TaskContext{})->sound());
    // The metadata now names the rebuilt WIM, and its own SHA-1 is the new one (read checks it).
    const std::string meta = bytes(table->entries[3]);
    CHECK(meta.find(std::string(reinterpret_cast<const char*>(rebuiltHash.data()), 20)) == 1000);
    CHECK(meta.find(std::string(reinterpret_cast<const char*>(innerHash.data()), 20)) == std::string::npos);
    CHECK(verifyWim(**file, TaskContext{})->sound());
}
