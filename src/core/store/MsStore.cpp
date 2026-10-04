#include "core/store/MsStore.h"

#include "base/Encoding.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/net/Http.h"
#include "core/system/Hash.h"

#include <json.hpp>
#include <pugixml.hpp>

#include <windows.h>
#include <combaseapi.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <map>

namespace wl::core {

namespace {

constexpr std::wstring_view kSearchUrl = L"https://storeedgefd.dsx.mp.microsoft.com/v9.0/manifestSearch";
constexpr std::wstring_view kProductUrl = L"https://displaycatalog.mp.microsoft.com/v7.0/products/";
constexpr std::wstring_view kFe3 = L"https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx";
constexpr std::wstring_view kFe3Secured = L"https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx/secured";

std::wstring wide(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? utf8::toWide(it->get<std::string>()) : std::wstring();
}

// "Name_1.2.3.4_x64__publisher" → its fields.
std::vector<std::wstring_view> nameParts(std::wstring_view fullName) {
    std::vector<std::wstring_view> parts;
    std::size_t start = 0;
    while (start <= fullName.size()) {
        const auto end = std::min(fullName.find(L'_', start), fullName.size());
        parts.push_back(fullName.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

// "2.2638.102.0" → comparable.
std::vector<int> versionKey(std::wstring_view version) {
    std::vector<int> key;
    std::size_t start = 0;
    while (start <= version.size()) {
        const auto end = std::min(version.find(L'.', start), version.size());
        try {
            key.push_back(std::stoi(std::wstring(version.substr(start, end - start))));
        } catch (...) {
            key.push_back(0);
        }
        start = end + 1;
    }
    return key;
}

std::string utcNow(std::chrono::minutes ahead = std::chrono::minutes(0)) {
    const auto t = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now() + ahead);
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", t);
}

std::string uuid() {
    GUID g{};
    CoCreateGuid(&g);
    return std::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", g.Data1, g.Data2, g.Data3, g.Data4[0],
                       g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

// What the Store client says it runs on (Windows Update decides by it which packages fit).
constexpr std::string_view kDevice =
    "E:BranchReadinessLevel=CBB&amp;CurrentBranch=ge_release&amp;FlightRing=Retail&amp;AttrDataVer=331&amp;InstallLanguage=en-US"
    "&amp;OSUILocale=en-US&amp;InstallationType=Client&amp;FlightingBranchName=&amp;OSSkuId=48&amp;App=WU&amp;ProcessorManufacturer=GenuineIntel"
    "&amp;OSArchitecture=AMD64&amp;IsDeviceRetailDemo=0&amp;IsFlightingEnabled=0&amp;TelemetryLevel=1&amp;OSVersion=10.0.26100.1"
    "&amp;IsRetailOS=1&amp;WuClientVer=10.0.26100.1&amp;DeviceFamily=Windows.Desktop&amp;Free=gt64";

std::string soap(std::string_view action, std::wstring_view to, std::string_view body) {
    return std::format(
        R"(<s:Envelope xmlns:a="http://www.w3.org/2005/08/addressing" xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Header>)"
        R"(<a:Action s:mustUnderstand="1">http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService/{}</a:Action>)"
        R"(<a:MessageID>urn:uuid:{}</a:MessageID><a:To s:mustUnderstand="1">{}</a:To>)"
        R"(<o:Security s:mustUnderstand="1" xmlns:o="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd">)"
        R"(<Timestamp xmlns="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd"><Created>{}</Created><Expires>{}</Expires></Timestamp>)"
        R"(<wuws:WindowsUpdateTicketsToken wsu:id="ClientMSA" xmlns:wsu="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd" xmlns:wuws="http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization">)"
        R"(<TicketType Name="MSA" Version="1.0" Policy="MBI_SSL"><User /></TicketType></wuws:WindowsUpdateTicketsToken></o:Security></s:Header>)"
        R"(<s:Body>{}</s:Body></s:Envelope>)",
        action, uuid(), utf8::fromWide(to), utcNow(), utcNow(std::chrono::minutes(5)), body);
}

Result<std::string> postSoap(std::wstring_view url, const std::string& envelope, const CancelToken& cancel) {
    auto r = httpPost(url, envelope, L"application/soap+xml; charset=utf-8", cancel);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->status != 200) {
        return fail(ErrorCode::IoError, std::format(L"Windows Update answered HTTP {}", r->status), std::wstring(url));
    }
    return std::move(r->body);
}

pugi::xml_node byName(const pugi::xml_node& root, const char* name) {
    return root.select_node(std::format("//*[local-name()='{}']", name).c_str()).node();
}

} // namespace

std::wstring StorePackage::version() const {
    const auto parts = nameParts(fullName);
    return parts.size() > 1 ? std::wstring(parts[1]) : std::wstring();
}

std::wstring StorePackage::architecture() const {
    const auto parts = nameParts(fullName);
    return parts.size() > 2 ? text::lower(parts[2]) : std::wstring();
}

Result<std::vector<StoreSearchResult>> parseStoreSearch(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"the Store search did not answer with JSON");
    }
    std::vector<StoreSearchResult> results;
    for (const auto& item : doc.value("Data", nlohmann::json::array())) {
        StoreSearchResult r{wide(item, "PackageIdentifier"), wide(item, "PackageName"), wide(item, "Publisher")};
        // Store products are 12 characters ("9NKSQGP7F2NH"); winget ids ("Mozilla.Firefox") are not Store apps.
        if (r.productId.size() == 12 && std::ranges::all_of(r.productId, [](wchar_t c) { return std::iswalnum(c) != 0; })) {
            results.push_back(std::move(r));
        }
    }
    return results;
}

Result<StoreProduct> parseStoreProduct(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("Product") || !doc["Product"].is_object()) {
        return fail(ErrorCode::NotFound, L"the Store catalogue has no such product");
    }
    const auto& p = doc["Product"];
    StoreProduct product;
    product.productId = wide(p, "ProductId");
    if (const auto lp = p.find("LocalizedProperties"); lp != p.end() && lp->is_array() && !lp->empty()) {
        product.title = wide((*lp)[0], "ProductTitle");
        product.publisher = wide((*lp)[0], "PublisherName");
        product.description = wide((*lp)[0], "ShortDescription");
    }
    for (const auto& sku : p.value("DisplaySkuAvailabilities", nlohmann::json::array())) {
        const auto props = sku.value("Sku", nlohmann::json::object()).value("Properties", nlohmann::json::object());
        if (const auto f = props.find("FulfillmentData"); f != props.end() && f->is_object() && product.wuCategoryId.empty()) {
            product.wuCategoryId = wide(*f, "WuCategoryId");
            product.packageFamilyName = wide(*f, "PackageFamilyName");
        }
        for (const auto& pkg : props.value("Packages", nlohmann::json::array())) {
            if (const auto size = pkg.find("MaxDownloadSizeInBytes"); size != pkg.end() && size->is_number()) {
                product.size = std::max(product.size, size->get<std::uint64_t>());
            }
        }
    }
    if (product.wuCategoryId.empty()) {
        return fail(ErrorCode::Unsupported, L"this product is not delivered through Windows Update (not an app package)",
                    product.productId);
    }
    return product;
}

Result<std::vector<StorePackage>> parseSyncUpdates(std::string_view soapText) {
    pugi::xml_document doc;
    if (!doc.load_buffer(soapText.data(), soapText.size())) {
        return fail(ErrorCode::ParseError, L"Windows Update did not answer with XML");
    }
    // NewUpdates: numeric id → identity (UpdateID, revision) and whether it is a framework.
    struct Identity {
        std::wstring updateId;
        int revision = 1;
        bool framework = false;
    };
    std::map<std::string, Identity> identities;
    for (const auto& info : doc.select_nodes("//*[local-name()='NewUpdates']/*[local-name()='UpdateInfo']")) {
        const std::string id = info.node().child_value("ID");
        pugi::xml_document fragment;
        const std::string xml = std::string("<r>") + info.node().child_value("Xml") + "</r>";
        if (id.empty() || !fragment.load_string(xml.c_str())) {
            continue;
        }
        const auto identity = fragment.child("r").child("UpdateIdentity");
        const auto properties = fragment.child("r").child("Properties");
        identities[id] = Identity{utf8::toWide(identity.attribute("UpdateID").value()), identity.attribute("RevisionNumber").as_int(1),
                                  std::string_view(properties.attribute("IsAppxFramework").value()) == "true"};
    }
    std::vector<StorePackage> packages;
    for (const auto& update : doc.select_nodes("//*[local-name()='ExtendedUpdateInfo']//*[local-name()='Update']")) {
        const std::string id = update.node().child_value("ID");
        const auto known = identities.find(id);
        pugi::xml_document fragment;
        const std::string xml = std::string("<r>") + update.node().child_value("Xml") + "</r>";
        if (known == identities.end() || !fragment.load_string(xml.c_str())) {
            continue;
        }
        const auto root = fragment.child("r");
        const auto main = root.child("HandlerSpecificData").child("AppxPackageInstallData").attribute("PackageFileName").value();
        if (!*main) {
            continue;
        }
        for (const auto& file : root.child("Files").children("File")) {
            if (std::string_view(file.attribute("FileName").value()) != main) {
                continue;
            }
            StorePackage p;
            p.fullName = utf8::toWide(file.attribute("InstallerSpecificIdentifier").value());
            p.identityName = utf8::toWide(root.child("ExtendedProperties").attribute("PackageIdentityName").value());
            const std::wstring fileName = utf8::toWide(main);
            p.extension = text::lower(fileName.substr(std::min(fileName.rfind(L'.'), fileName.size())));
            p.size = file.attribute("Size").as_ullong();
            for (const auto& digest : file.children("AdditionalDigest")) {
                if (std::string_view(digest.attribute("Algorithm").value()) == "SHA256") {
                    p.sha256 = hexLower(base64Decode(digest.child_value()));
                }
            }
            p.updateId = known->second.updateId;
            p.revision = known->second.revision;
            p.framework = known->second.framework ||
                          std::string_view(root.child("ExtendedProperties").attribute("IsAppxFramework").value()) == "true";
            if (!p.fullName.empty() && !p.updateId.empty()) {
                packages.push_back(std::move(p));
            }
        }
    }
    return packages;
}

std::vector<std::wstring> parseFileUrls(std::string_view soapText) {
    std::vector<std::wstring> urls;
    pugi::xml_document doc;
    if (!doc.load_buffer(soapText.data(), soapText.size())) {
        return urls;
    }
    for (const auto& url : doc.select_nodes("//*[local-name()='FileLocation']/*[local-name()='Url']")) {
        urls.push_back(utf8::toWide(url.node().child_value()));
    }
    return urls;
}

std::vector<StorePackage> pickStorePackages(std::span<const StorePackage> all, std::wstring_view familyName,
                                            std::wstring_view architecture) {
    const std::wstring arch = text::lower(architecture);
    const std::wstring identity = std::wstring(familyName.substr(0, familyName.rfind(L'_')));
    // Encrypted packages (.eappx, .emsixbundle …: the Store's DRM) need the Store's licence to be
    // installed: they cannot go into an image.
    auto fits = [&](const StorePackage& p) {
        const auto a = p.architecture();
        return (a == arch || a == L"neutral") && !p.extension.starts_with(L".e");
    };
    auto newer = [](const StorePackage& a, const StorePackage& b) { return versionKey(a.version()) > versionKey(b.version()); };
    std::vector<StorePackage> picked;
    // The app: its newest bundle (every architecture in one) or, without one, its newest package for the image.
    const StorePackage* app = nullptr;
    for (const auto& p : all) {
        if (p.framework || _wcsicmp(p.identityName.c_str(), identity.c_str()) != 0 || !fits(p)) {
            continue;
        }
        const bool bundle = p.extension.ends_with(L"bundle");
        const bool appBundle = app && app->extension.ends_with(L"bundle");
        if (!app || (bundle && !appBundle) || (bundle == appBundle && newer(p, *app))) {
            app = &p;
        }
    }
    if (!app) {
        return picked;
    }
    picked.push_back(*app);
    // Frameworks: the newest per identity, for the image's architecture.
    std::map<std::wstring, const StorePackage*> frameworks;
    for (const auto& p : all) {
        if (!p.framework || p.architecture() != arch) {
            continue;
        }
        auto& best = frameworks[text::lower(p.identityName)];
        if (!best || newer(p, *best)) {
            best = &p;
        }
    }
    for (const auto& [name, p] : frameworks) {
        picked.push_back(*p);
    }
    return picked;
}

bool trustedStoreUrl(std::wstring_view url) {
    const std::wstring u = text::lower(url);
    std::wstring_view rest;
    if (u.starts_with(L"https://")) {
        rest = std::wstring_view(u).substr(8);
    } else if (u.starts_with(L"http://")) {
        rest = std::wstring_view(u).substr(7);
    } else {
        return false;
    }
    const std::wstring_view authority = rest.substr(0, rest.find_first_of(L"/?#"));
    if (authority.find(L'@') != std::wstring_view::npos) {
        return false;
    }
    const std::wstring_view host = authority.substr(0, authority.find(L':'));
    return host.size() > 14 && host.ends_with(L".microsoft.com");
}

Result<std::vector<StoreSearchResult>> searchStore(std::wstring_view query, const CancelToken& cancel) {
    const nlohmann::json body{{"Query", {{"KeyWord", utf8::fromWide(query)}, {"MatchType", "Substring"}}}};
    auto r = httpPost(kSearchUrl, body.dump(), L"application/json", cancel);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->status != 200) {
        return fail(ErrorCode::IoError, std::format(L"the Store search answered HTTP {}", r->status), std::wstring(kSearchUrl));
    }
    return parseStoreSearch(r->body);
}

Result<StoreProduct> storeProduct(std::wstring_view productId, const CancelToken& cancel) {
    if (productId.size() != 12 || !std::ranges::all_of(productId, [](wchar_t c) { return std::iswalnum(c) != 0; })) {
        return fail(ErrorCode::InvalidArgument, L"not a Store product id", std::wstring(productId));
    }
    const std::wstring url = std::wstring(kProductUrl) + std::wstring(productId) + L"?market=US&languages=en-US&fieldsTemplate=Details";
    auto r = httpGet(url, cancel);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->status != 200) {
        return fail(ErrorCode::NotFound, std::format(L"the Store catalogue answered HTTP {}", r->status), url);
    }
    return parseStoreProduct(r->body);
}

Result<std::vector<StorePackage>> storePackages(const StoreProduct& product, const CancelToken& cancel) {
    const std::string cookieBody = std::format(
        R"(<GetCookie xmlns="http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService"><oldCookie></oldCookie>)"
        R"(<lastChange>2015-10-21T17:01:07.1472913Z</lastChange><currentTime>{}</currentTime><protocolVersion>1.81</protocolVersion></GetCookie>)",
        utcNow());
    auto cookieReply = postSoap(kFe3, soap("GetCookie", kFe3, cookieBody), cancel);
    if (!cookieReply) {
        return std::unexpected(cookieReply.error());
    }
    pugi::xml_document cookieDoc;
    cookieDoc.load_buffer(cookieReply->data(), cookieReply->size());
    const std::string cookie = byName(cookieDoc, "EncryptedData").child_value();
    if (cookie.empty()) {
        return fail(ErrorCode::IoError, L"Windows Update gave no cookie");
    }
    // The ids the Store client always reports as installed (the platform's own updates).
    static constexpr int kInstalled[] = {1, 2, 3, 11, 19, 544, 549, 2359974, 2359977, 5169044, 8788830, 23110993, 23110994, 54341900,
                                         54343656, 59830006, 59830007, 59830008, 60484010, 62450018, 62450019, 62450020, 66027979,
                                         66053150, 97657898, 98822896, 98959022, 98959023, 98959024, 98959025, 98959026, 104433538,
                                         104900364, 105489019, 117765322, 129905029, 130040031, 132387090, 132393049, 133399034,
                                         138537048, 140377312, 143747671, 158941041, 158941042, 158941043, 158941044, 159123858,
                                         159130928, 164836897, 164847386, 164848327, 164852241, 164852246, 164852252, 164852253};
    std::string installed;
    for (const int id : kInstalled) {
        installed += std::format("<int>{}</int>", id);
    }
    const std::string syncBody = std::format(
        R"(<SyncUpdates xmlns="http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService"><cookie><Expiration>2045-03-11T02:02:48Z</Expiration>)"
        R"(<EncryptedData>{}</EncryptedData></cookie><parameters><ExpressQuery>false</ExpressQuery><InstalledNonLeafUpdateIDs>{}</InstalledNonLeafUpdateIDs>)"
        R"(<OtherCachedUpdateIDs></OtherCachedUpdateIDs><SkipSoftwareSync>false</SkipSoftwareSync><NeedTwoGroupOutOfScopeUpdates>true</NeedTwoGroupOutOfScopeUpdates>)"
        R"(<FilterAppCategoryIds><CategoryIdentifier><Id>{}</Id></CategoryIdentifier></FilterAppCategoryIds><TreatAppCategoryIdsAsInstalled>true</TreatAppCategoryIdsAsInstalled>)"
        R"(<AlsoPerformRegularSync>false</AlsoPerformRegularSync><ComputerSpec/><ExtendedUpdateInfoParameters><XmlUpdateFragmentTypes>)"
        R"(<XmlUpdateFragmentType>Extended</XmlUpdateFragmentType></XmlUpdateFragmentTypes><Locales><string>en-US</string><string>en</string></Locales>)"
        R"(</ExtendedUpdateInfoParameters><ClientPreferredLanguages><string>en-US</string></ClientPreferredLanguages><ProductsParameters>)"
        R"(<SyncCurrentVersionOnly>false</SyncCurrentVersionOnly><DeviceAttributes>{}</DeviceAttributes>)"
        R"(<CallerAttributes>E:Interactive=1&amp;IsSeeker=1&amp;Acquisition=1&amp;SheddingAware=1&amp;Id=Acquisition%3BMicrosoft.WindowsStore_8wekyb3d8bbwe&amp;</CallerAttributes>)"
        R"(<Products/></ProductsParameters></parameters></SyncUpdates>)",
        cookie, installed, utf8::fromWide(product.wuCategoryId), kDevice);
    auto sync = postSoap(kFe3, soap("SyncUpdates", kFe3, syncBody), cancel);
    if (!sync) {
        return std::unexpected(sync.error());
    }
    auto packages = parseSyncUpdates(*sync);
    if (packages) {
        log::info("store", std::format(L"{}: {} package(s) on Windows Update", product.title, packages->size()));
    }
    return packages;
}

Result<std::vector<std::filesystem::path>> downloadStorePackages(std::span<const StorePackage> packages,
                                                                 const std::filesystem::path& folder, const TaskContext& task) {
    std::uint64_t total = 0;
    for (const auto& p : packages) {
        total += p.size;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const TaskContext quiet{task.cancel, {}};
    std::uint64_t base = 0;
    std::vector<std::filesystem::path> saved;
    for (const auto& p : packages) {
        const std::wstring name = p.fileName();
        if (name.empty() || name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos || p.sha256.size() != 64) {
            return fail(ErrorCode::InvalidArgument, L"unexpected package in the Windows Update answer", name);
        }
        const auto target = folder / name;
        bool have = false;
        if (std::filesystem::is_regular_file(target, ec)) {
            auto hash = sha256File(target, quiet);
            have = hash && *hash == p.sha256;
        }
        if (!have) {
            const std::string body = std::format(
                R"(<GetExtendedUpdateInfo2 xmlns="http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService"><updateIDs><UpdateIdentity>)"
                R"(<UpdateID>{}</UpdateID><RevisionNumber>{}</RevisionNumber></UpdateIdentity></updateIDs><infoTypes><XmlUpdateFragmentType>FileUrl</XmlUpdateFragmentType>)"
                R"(<XmlUpdateFragmentType>FileDecryption</XmlUpdateFragmentType></infoTypes><deviceAttributes>{}</deviceAttributes></GetExtendedUpdateInfo2>)",
                utf8::fromWide(p.updateId), p.revision, kDevice);
            auto reply = postSoap(kFe3Secured, soap("GetExtendedUpdateInfo2", kFe3Secured, body), task.cancel);
            if (!reply) {
                return std::unexpected(reply.error());
            }
            const auto urls = parseFileUrls(*reply);
            // The package's own file: its address carries the file's GUID name (the others are the block maps).
            const auto url = std::ranges::find_if(urls, [](const std::wstring& u) { return u.find(L"tlu.dl.delivery") != std::wstring::npos; });
            const std::wstring chosen = url != urls.end() ? *url : urls.empty() ? std::wstring() : urls.front();
            if (!trustedStoreUrl(chosen)) {
                return fail(ErrorCode::AccessDenied, L"download address is not a Microsoft server", chosen);
            }
            auto got = httpDownload(chosen, target, p.size, quiet,
                                    [&](std::uint64_t done, std::uint64_t) {
                                        task.report(total ? std::min(static_cast<double>(base + done) / static_cast<double>(total), 1.0) : -1.0,
                                                    L"download");
                                    },
                                    [](std::wstring_view finalUrl) { return trustedStoreUrl(finalUrl); });
            if (!got) {
                return std::unexpected(got.error());
            }
            auto hash = sha256File(target, quiet);
            if (!hash || *hash != p.sha256) {
                std::filesystem::remove(target, ec);
                return fail(ErrorCode::IoError, L"the downloaded package does not match its SHA-256", target.wstring());
            }
            log::info("store", std::format(L"downloaded {} ({} bytes, SHA-256 ok)", target.wstring(), *got));
        }
        base += p.size;
        saved.push_back(target);
    }
    task.report(1.0, L"done");
    return saved;
}

} // namespace wl::core
