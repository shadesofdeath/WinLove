// D-149: Microsoft's official ESD catalog (the Media Creation Tool's products.cab) — parsing, the
// cabinet, the metadata service's answer, which files a selection needs, the source choice, the
// local state of a download, the hash check and the ESD's layout. Fixtures: trimmed copies of the
// real catalogs of 2026-11-11 (tests/integration/fixtures/esd-products-*.xml, esd-products.cab).
#include "core/esd/EsdCatalog.h"
#include "core/esd/EsdConvert.h"
#include "core/io/Cabinet.h"

#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

using namespace wl;
using namespace wl::core;
using namespace wl::core::esd;

namespace {

std::string fixture(const wchar_t* name) {
    std::ifstream in(std::filesystem::path(WL_SOURCE_DIR) / L"tests/integration/fixtures" / name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Catalog win11() {
    auto c = parseProductsXml(fixture(L"esd-products-win11.xml"), Product::Windows11);
    REQUIRE(c);
    return *c;
}

uup::Build build(std::wstring title, std::wstring number, std::wstring arch = L"amd64") {
    uup::Build b;
    b.id = L"id";
    b.title = std::move(title);
    b.build = std::move(number);
    b.arch = std::move(arch);
    b.kind = uup::buildKind(b.title);
    return b;
}

const std::vector<std::wstring> kPro{L"PROFESSIONAL"};

} // namespace

TEST_CASE("ESD catalog: Windows 11 (catalog 2.1, SHA-256) parses into entries") {
    const Catalog c = win11();
    CHECK(c.version == L"2.1");
    CHECK(c.entries.size() == 24);
    CHECK(c.build() == L"26300.9711");
    const auto it = std::ranges::find_if(c.entries, [](const Entry& e) {
        return e.language == L"tr-tr" && e.architecture == L"x64" && e.edition == L"Professional";
    });
    REQUIRE(it != c.entries.end());
    CHECK(it->fileName == L"26300.9711.261011-1601.26h2_ge_release_svc_refresh_CLIENTCONSUMER_RET_x64FRE_tr-tr.esd");
    CHECK(it->size == 6096708340ull);
    CHECK(it->sha256 == L"b0a405780d4cbd8f5871a3ee8b058cc6e0a958252004d4296916f9bf503026d4");
    CHECK(it->sha1.empty());
    CHECK(it->build == L"26300.9711");
    CHECK(it->stamp == L"261011-1601");
    CHECK_FALSE(it->retailOnly);
    CHECK(it->url.starts_with(L"http://dl.delivery.mp.microsoft.com/filestreamingservice/files/"));
    CHECK(it->languageName == L"Turkish (Türkiye)");
    // ARM64 is lower-cased like UUP's architectures.
    CHECK(std::ranges::any_of(c.entries, [](const Entry& e) { return e.architecture == L"arm64"; }));
}

TEST_CASE("ESD catalog: Windows 10 (catalog 1.4.1, SHA-1)") {
    auto c = parseProductsXml(fixture(L"esd-products-win10.xml"), Product::Windows10);
    REQUIRE(c);
    CHECK(c->version == L"1.4.1");
    CHECK(c->build() == L"19045.3803");
    const auto it = std::ranges::find_if(c->entries, [](const Entry& e) {
        return e.language == L"tr-tr" && e.architecture == L"x64" && e.edition == L"Professional";
    });
    REQUIRE(it != c->entries.end());
    CHECK(it->sha1 == L"93b697ff8e6f2bc0d8a72ec39bbd91e87a55bb20");
    CHECK(it->sha256.empty());
    CHECK(it->size == 3926396442ull);
    CHECK(it->stamp == L"231204-0204");
}

TEST_CASE("ESD catalog: what is not a usable entry is left out; not a catalog is an error") {
    const char* xml = R"(<MCT><Catalogs><Catalog version="2.1"><PublishedMedia><Files>
        <File><FileName>26300.9711.261011-1601.x_tr-tr.esd</FileName><LanguageCode>tr-tr</LanguageCode><Edition>Core</Edition>
          <Architecture>x64</Architecture><Size>10</Size><Sha256>00</Sha256><FilePath>http://dl.delivery.mp.microsoft.com/a</FilePath></File>
        <File><FileName>..\evil.esd</FileName><LanguageCode>tr-tr</LanguageCode><Edition>Core</Edition><Architecture>x64</Architecture>
          <Size>10</Size><Sha1>0123456789012345678901234567890123456789</Sha1><FilePath>http://dl.delivery.mp.microsoft.com/a</FilePath></File>
        <File><FileName>26300.9711.261011-1601.x_tr-tr.esd</FileName><LanguageCode>TR-TR</LanguageCode><Edition>Core</Edition>
          <Architecture>x64</Architecture><Size>10</Size><Sha1>0123456789012345678901234567890123456789</Sha1><FilePath>http://dl.delivery.mp.microsoft.com/a</FilePath></File>
        </Files></PublishedMedia></Catalog></Catalogs></MCT>)";
    auto c = parseProductsXml(xml, Product::Windows11);
    REQUIRE(c);
    REQUIRE(c->entries.size() == 1); // a short hash and a path in the name are not taken
    CHECK(c->entries[0].language == L"tr-tr");
    CHECK_FALSE(parseProductsXml("<x/>", Product::Windows11));
    CHECK_FALSE(parseProductsXml("not xml", Product::Windows11));
}

TEST_CASE("ESD catalog: products.xml out of a cabinet in memory (FDI)") {
    const std::string cab = fixture(L"esd-products.cab");
    REQUIRE(cab.size() > 100);
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(cab.data()), cab.size());
    auto names = cabinetNames(bytes);
    REQUIRE(names);
    REQUIRE(names->size() == 1);
    CHECK((*names)[0] == "products.xml");
    auto xml = cabinetFile(bytes, "PRODUCTS.XML");
    REQUIRE(xml);
    CHECK(*xml == fixture(L"esd-products-win11.xml"));
    auto none = cabinetFile(bytes, "other.xml");
    REQUIRE_FALSE(none);
    CHECK(none.error().code == ErrorCode::NotFound);
    CHECK_FALSE(cabinetFile(bytes, "products.xml", 100)); // larger than allowed
    const std::string junk = "MSCF\0\0\0\0garbage garbage garbage garbage garbage";
    CHECK_FALSE(cabinetFile(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(junk.data()), junk.size()), "a"));
    CHECK_FALSE(cabinetNames({}));
}

TEST_CASE("ESD catalog: Windows Update's metadata service answer → products.cab") {
    const char* json =
        R"([{"UpdateIds":["80342f72-3aaf-4ff2-bc52-56f4ff339a0e"],"FileLocations":[{"FileName":"products.cab","Size":43203,"Language":null,)"
        R"("Digest":"ND/Kj32kr+r+ksEme3iHd4z9SythvQrNYeZOqBBMbgA=","ContentType":"Metadata",)"
        R"("Url":"http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/76250a93-ee34-4cf1-bbd2-4bf8a32d79c5?P1=1794483816&P2=404"}]}])";
    auto where = parseCabSearch(json);
    REQUIRE(where);
    CHECK(where->size == 43203);
    CHECK(where->sha256 == L"343fca8f7da4afeafe92c1267b7887778cfd4b2b61bd0acd61e64ea8104c6e00");
    CHECK(where->url.starts_with(L"http://tlu.dl.delivery.mp.microsoft.com/"));
    auto empty = parseCabSearch("[]");
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == ErrorCode::NotFound);
    CHECK_FALSE(parseCabSearch("{}"));
    CHECK_FALSE(parseCabSearch(R"([{"FileLocations":[{"FileName":"products.cab","Url":"http://x.microsoft.com/a","Digest":"AAAA"}]}])"));
}

TEST_CASE("ESD catalog: small helpers") {
    CHECK(buildFromFileName(L"26300.9711.261011-1601.26h2_ge_release_svc_refresh_CLIENTCONSUMER_RET_x64FRE_tr-tr.esd") ==
          std::pair<std::wstring, std::wstring>{L"26300.9711", L"261011-1601"});
    CHECK(buildFromFileName(L"19045.3803.231204-0204.22h2_release").first == L"19045.3803");
    CHECK(buildFromFileName(L"install.esd").first.empty());
    CHECK(majorBuild(L"26300.9711") == 26300);
    CHECK(revision(L"26300.9711") == 9711);
    CHECK(majorBuild(L"x") == 0);
    CHECK(catalogArchitecture(L"amd64") == L"x64");
    CHECK(catalogArchitecture(L"ARM64") == L"arm64");
    CHECK(catalogArchitecture(L"x86") == L"x86");
    CHECK(trustedEsdUrl(L"http://dl.delivery.mp.microsoft.com/filestreamingservice/files/x/y.esd"));
    CHECK(trustedEsdUrl(L"https://download.microsoft.com/download/a/products.cab"));
    CHECK_FALSE(trustedEsdUrl(L"http://microsoft.com.evil.example/a.esd"));
    CHECK_FALSE(trustedEsdUrl(L"http://dl.delivery.mp.microsoft.com@evil.example/a.esd"));
    CHECK_FALSE(trustedEsdUrl(L"ftp://dl.delivery.mp.microsoft.com/a.esd"));
    CHECK_FALSE(trustedEsdUrl(L"http://notmicrosoft.com/a.esd"));
    CHECK(productOf(build(L"Windows 11, version 26H2 (26300.9711)", L"26300.9711")) == Product::Windows11);
    CHECK(productOf(build(L"Feature update to Windows 10, version 22H2 (19045.7727)", L"19045.7727")) == Product::Windows10);
    CHECK_FALSE(productOf(build(L"Windows Server 2025 (26100.6899)", L"26100.6899")));
}

TEST_CASE("ESD catalog: the files of a selection — one per distinct ESD") {
    const Catalog c = win11();
    const std::vector<std::wstring> proHome{L"PROFESSIONAL", L"CORE"};
    auto one = pickFiles(c, L"tr-tr", L"amd64", proHome);
    CHECK(one.missing.empty());
    REQUIRE(one.files.size() == 1); // Pro and Home share the consumer ESD
    CHECK(one.files[0].fileName.find(L"CLIENTCONSUMER") != std::wstring::npos);
    CHECK(one.bytes() == 6096708340ull);
    const std::vector<std::wstring> proEnt{L"Professional", L"Enterprise"};
    auto two = pickFiles(c, L"tr-tr", L"x64", proEnt);
    REQUIRE(two.files.size() == 2); // Enterprise is in the business ESD
    CHECK(two.files[1].fileName.find(L"CLIENTBUSINESS") != std::wstring::npos);
    const std::vector<std::wstring> odd{L"PROFESSIONAL", L"SERVERDATACENTER"};
    auto missing = pickFiles(c, L"tr-tr", L"x64", odd);
    REQUIRE(missing.missing.size() == 1);
    CHECK(missing.missing[0] == L"SERVERDATACENTER");
    // Listed in the catalog against the consumer ESD, but no image of their own in it (lab 2026-11-11: the
    // 26H2 consumer ESD holds Home, Home Single Language, Education and Pro).
    const std::vector<std::wstring> workstation{L"PROFESSIONAL", L"PROFESSIONALWORKSTATION"};
    auto mapped = pickFiles(c, L"tr-tr", L"x64", workstation);
    CHECK(mapped.missing == std::vector<std::wstring>{L"PROFESSIONALWORKSTATION"});
    CHECK(imageEdition(L"CORESINGLELANGUAGE"));
    CHECK_FALSE(imageEdition(L"ProfessionalN"));
    CHECK(pickFiles(c, L"tr-tr", L"arm64", kPro).files.at(0).fileName.find(L"A64FRE") != std::wstring::npos);
    CHECK(pickFiles(c, L"de-de", L"x64", kPro).missing.size() == 1);
}

TEST_CASE("ESD catalog: the source a selection gets, and why") {
    const Catalog c = win11();
    const auto latest = build(L"Windows 11, version 26H2 (26300.9711)", L"26300.9711");
    const auto newer = build(L"Windows 11, version 26H2 (26300.9802)", L"26300.9802");
    const auto older = build(L"Windows 11, version 26H2 (26300.9550)", L"26300.9550");

    auto exact = chooseSource(&c, false, latest, false, L"tr-tr", kPro);
    CHECK(exact.source == Source::Esd);
    CHECK(exact.esdAllowed);
    CHECK(exact.reason == Reason::Exact);
    CHECK(exact.esdBuild == L"26300.9711");
    CHECK(exact.esdStamp == L"261011-1601");
    CHECK(exact.pick.files.size() == 1);

    // The newest of its release in the list, the catalog a month behind: the official ESD.
    auto newest = chooseSource(&c, false, newer, true, L"tr-tr", kPro);
    CHECK(newest.source == Source::Esd);
    CHECK(newest.reason == Reason::Newest);
    // An older build picked on purpose: UUP gives that build; ESD may still be chosen.
    auto other = chooseSource(&c, false, older, false, L"tr-tr", kPro);
    CHECK(other.source == Source::Uup);
    CHECK(other.esdAllowed);
    CHECK(other.reason == Reason::OtherBuild);

    auto loading = chooseSource(nullptr, false, latest, true, L"tr-tr", kPro);
    CHECK(loading.reason == Reason::Loading);
    CHECK_FALSE(loading.esdAllowed);
    CHECK(chooseSource(nullptr, true, latest, true, L"tr-tr", kPro).reason == Reason::Failed);
    CHECK(chooseSource(&c, false, build(L"Windows 11 Insider Preview 27965.1000 (rs_prerelease)", L"27965.1000"), true, L"tr-tr", kPro)
              .reason == Reason::NotRelease);
    auto noRelease = chooseSource(&c, false, build(L"Windows 11, version 25H2 (26200.9470)", L"26200.9470"), true, L"tr-tr", kPro);
    CHECK(noRelease.reason == Reason::NoRelease);
    CHECK(noRelease.esdBuild == L"26300.9711"); // what the catalog has instead
    CHECK_FALSE(noRelease.esdAllowed);
    CHECK(chooseSource(&c, false, build(L"Windows 11, version 26H2 (26300.9711)", L"26300.9711", L"x86"), true, L"tr-tr", kPro)
              .reason == Reason::NoArch);
    CHECK(chooseSource(&c, false, latest, true, L"de-de", kPro).reason == Reason::NoLanguage);
    const std::vector<std::wstring> server{L"SERVERDATACENTER"};
    auto noEdition = chooseSource(&c, false, latest, true, L"tr-tr", server);
    CHECK(noEdition.reason == Reason::NoEdition);
    CHECK(noEdition.missingEdition == L"SERVERDATACENTER");
    CHECK_FALSE(noEdition.esdAllowed);
    CHECK(chooseSource(&c, false, latest, true, L"tr-tr", {}).reason == Reason::NoEdition); // nothing ticked

    // The user's switch: honoured when allowed, never into a disabled ESD.
    CHECK(effectiveSource(exact, std::nullopt) == Source::Esd);
    CHECK(effectiveSource(exact, Source::Uup) == Source::Uup);
    CHECK(effectiveSource(other, Source::Esd) == Source::Esd);
    CHECK(effectiveSource(other, std::nullopt) == Source::Uup);
    CHECK(effectiveSource(noEdition, Source::Esd) == Source::Uup);
    CHECK(effectiveSource(loading, Source::Esd) == Source::Uup);
}

TEST_CASE("ESD download: local state decides between continuing, verifying and starting over") {
    CHECK(localState(std::nullopt, std::nullopt, 100) == LocalState::Missing);
    CHECK(localState(std::nullopt, 40, 100) == LocalState::Partial);
    CHECK(localState(std::nullopt, 100, 100) == LocalState::Partial); // complete .part: the rename did not happen
    CHECK(localState(std::nullopt, 101, 100) == LocalState::WrongSize);
    CHECK(localState(100, std::nullopt, 100) == LocalState::Complete);
    CHECK(localState(100, 40, 100) == LocalState::Complete);
    CHECK(localState(99, std::nullopt, 100) == LocalState::WrongSize);
}

TEST_CASE("ESD download: the file is checked against its SHA-256, or SHA-1 in the older catalogs") {
    const auto file = std::filesystem::temp_directory_path() / std::format(L"wl-esd-verify-{}.esd", GetCurrentProcessId());
    {
        std::ofstream out(file, std::ios::binary);
        out << "abc";
    }
    Entry e;
    e.size = 3;
    e.sha256 = L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    auto ok = verifyFile(file, e, {});
    REQUIRE(ok);
    CHECK(*ok);
    e.sha256 = L"aa7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    CHECK_FALSE(*verifyFile(file, e, {}));
    e.sha256.clear();
    e.sha1 = L"a9993e364706816aba3e25717850c26c9cd0d89d";
    CHECK(*verifyFile(file, e, {}));
    e.sha1 = L"b9993e364706816aba3e25717850c26c9cd0d89d";
    CHECK_FALSE(*verifyFile(file, e, {}));
    e.sha1 = L"a9993e364706816aba3e25717850c26c9cd0d89d";
    e.size = 4; // another size is never hashed
    CHECK_FALSE(*verifyFile(file, e, {}));
    double last = -1;
    std::wstring stage;
    e.size = 3;
    CHECK(*verifyFile(file, e, TaskContext{{}, [&](double f, std::wstring_view s) {
                                               last = f;
                                               stage = s;
                                           }}));
    CHECK(last == doctest::Approx(1.0));
    CHECK(stage == L"verify");
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST_CASE("ESD convert: the layout of a Media Creation Tool ESD and the editions to export") {
    auto image = [](int index, std::wstring name, std::wstring editionId, std::wstring type, Architecture arch) {
        ImageInfo i;
        i.index = index;
        i.name = std::move(name);
        i.editionId = std::move(editionId);
        i.installationType = std::move(type);
        i.architecture = arch;
        i.defaultLanguage = L"tr-TR";
        return i;
    };
    const std::vector<ImageInfo> images{
        image(1, L"Windows Setup Media", L"", L"", Architecture::Unknown),
        image(2, L"Microsoft Windows PE (amd64)", L"WindowsPE", L"WindowsPE", Architecture::X64),
        image(3, L"Microsoft Windows Setup (amd64)", L"WindowsPE", L"WindowsPE", Architecture::X64),
        image(4, L"Windows 11 Home", L"Core", L"Client", Architecture::X64),
        image(5, L"Windows 11 Pro", L"Professional", L"Client", Architecture::X64),
        image(6, L"Windows 11 Education", L"Education", L"Client", Architecture::X64),
    };
    auto layout = esdLayout(images);
    REQUIRE(layout);
    CHECK(layout->media == 1);
    CHECK(layout->windowsPe == std::vector<int>{2, 3});
    REQUIRE(layout->editions.size() == 3);
    CHECK(layout->editions[1].editionId == L"Professional");
    CHECK(layout->editions[1].architecture == L"x64");
    CHECK(layout->editions[1].language == L"tr-TR");
    CHECK_FALSE(esdLayout(std::span<const ImageInfo>(images).subspan(1))); // no setup media: not an MCT ESD
    CHECK_FALSE(esdLayout(std::span<const ImageInfo>(images).first(3)));   // no edition

    const std::vector<std::pair<std::filesystem::path, Layout>> esds{{L"a.esd", *layout}};
    std::vector<std::wstring> missing;
    const std::vector<std::wstring> wanted{L"PROFESSIONAL", L"CORE", L"ENTERPRISE"};
    auto picked = selectEditions(esds, wanted, &missing);
    REQUIRE(picked.size() == 2);
    CHECK(picked[0].edition.index == 4); // the ESD's own order
    CHECK(picked[1].edition.index == 5);
    CHECK(missing == std::vector<std::wstring>{L"ENTERPRISE"});
    CHECK(selectEditions(esds, {}).size() == 3); // nothing asked: every edition
}
