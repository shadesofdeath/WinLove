#include "core/updates/WuDiscovery.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/net/Http.h"

#include <windows.h>
#include <combaseapi.h>

#include <chrono>
#include <format>
#include <cctype>
#include <optional>
#include <tuple>
#include <set>

namespace wl::core {

namespace {

constexpr std::wstring_view kFe3 = L"https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx";
constexpr std::string_view kNs = "http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService";

std::string utcNow(std::chrono::minutes ahead = std::chrono::minutes(0)) {
    const auto t = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now() + ahead);
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", t);
}

std::string uuid() {
    GUID g{};
    CoCreateGuid(&g);
    return std::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", g.Data1, g.Data2, g.Data3, g.Data4[0],
                       g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

// An anonymous client (empty MSA ticket), as Windows Update's own first contact.
std::string envelope(std::string_view action, std::string_view body) {
    return std::format(
        R"(<s:Envelope xmlns:a="http://www.w3.org/2005/08/addressing" xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Header>)"
        R"(<a:Action s:mustUnderstand="1">{}/{}</a:Action><a:MessageID>urn:uuid:{}</a:MessageID><a:To s:mustUnderstand="1">{}</a:To>)"
        R"(<o:Security s:mustUnderstand="1" xmlns:o="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd">)"
        R"(<Timestamp xmlns="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd"><Created>{}</Created><Expires>{}</Expires></Timestamp>)"
        R"(<wuws:WindowsUpdateTicketsToken wsu:id="ClientMSA" xmlns:wsu="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd" xmlns:wuws="http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization">)"
        R"(<TicketType Name="MSA" Version="1.0" Policy="MBI_SSL"/></wuws:WindowsUpdateTicketsToken></o:Security></s:Header><s:Body>{}</s:Body></s:Envelope>)",
        kNs, action, uuid(), utf8::fromWide(kFe3), utcNow(), utcNow(std::chrono::minutes(5)), body);
}

Result<std::string> post(std::string_view action, std::string_view body, const CancelToken& cancel) {
    auto r = httpPost(kFe3, envelope(action, body), L"application/soap+xml; charset=utf-8", cancel);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->status != 200) {
        return fail(ErrorCode::IoError, std::format(L"Windows Update answered HTTP {} ({})", r->status, utf8::toWide(action)),
                    std::wstring(kFe3));
    }
    return std::move(r->body);
}

std::string between(const std::string& text, std::string_view open, std::string_view close) {
    const auto a = text.find(open);
    if (a == std::string::npos) {
        return {};
    }
    const auto b = text.find(close, a + open.size());
    return b == std::string::npos ? std::string() : text.substr(a + open.size(), b - a - open.size());
}

std::string sync(const std::string& cookie, const std::string& device, const std::string& products, const CancelToken& cancel,
                 Error& error) {
    const std::string body = std::format(
        R"(<SyncUpdates xmlns="{}"><cookie><Expiration>{}</Expiration><EncryptedData>{}</EncryptedData></cookie><parameters>)"
        R"(<ExpressQuery>false</ExpressQuery><InstalledNonLeafUpdateIDs/><OtherCachedUpdateIDs/><SkipSoftwareSync>false</SkipSoftwareSync>)"
        R"(<NeedTwoGroupOutOfScopeUpdates>true</NeedTwoGroupOutOfScopeUpdates><AlsoPerformRegularSync>true</AlsoPerformRegularSync><ComputerSpec/>)"
        R"(<ExtendedUpdateInfoParameters><XmlUpdateFragmentTypes><XmlUpdateFragmentType>Extended</XmlUpdateFragmentType>)"
        R"(<XmlUpdateFragmentType>LocalizedProperties</XmlUpdateFragmentType></XmlUpdateFragmentTypes><Locales><string>en-US</string></Locales>)"
        R"(</ExtendedUpdateInfoParameters><ClientPreferredLanguages/><ProductsParameters><SyncCurrentVersionOnly>false</SyncCurrentVersionOnly>)"
        R"(<DeviceAttributes>{}</DeviceAttributes><CallerAttributes>E:Profile=AUv2&amp;Acquisition=1&amp;Interactive=1&amp;IsSeeker=1&amp;SheddingAware=1&amp;Id=MoUpdateOrchestrator</CallerAttributes>)"
        R"(<Products>{}</Products></ProductsParameters></parameters></SyncUpdates>)",
        kNs, utcNow(std::chrono::minutes(60)), cookie, device, products);
    auto reply = post("SyncUpdates", body, cancel);
    if (!reply) {
        error = reply.error();
        return {};
    }
    return std::move(*reply);
}

} // namespace

std::wstring wuBranch(int build) {
    switch (build) {
    case 17763: return L"rs5_release";
    case 18362:
    case 18363: return L"19h1_release";
    case 19041:
    case 19042:
    case 19043:
    case 19044:
    case 19045: return L"vb_release";
    case 22000: return L"co_release";
    case 22621:
    case 22631: return L"ni_release";
    case 26100:
    case 26200:
    case 26300: return L"ge_release";
    case 28000: return L"br_release";
    default: return {};
    }
}

std::vector<std::wstring> wuPackageKbs(std::string_view syncReply) {
    // File names of update packages, escaped or not inside the answer's XML fragments:
    // Windows11.0-KB5127648-x64.msu, Windows11.0-KB5127690-x64-NDP481.cab, windows10.0-kb….cab.
    // A plain scan (the answer is ~10 MB: no backtracking regex over it).
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    auto nameChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.'; };
    std::vector<std::wstring> out;
    std::set<std::wstring> seen;
    const std::size_t n = syncReply.size();
    for (std::size_t i = 0; i + 8 < n; ++i) {
        if (lower(syncReply[i]) != 'k' || lower(syncReply[i + 1]) != 'b') {
            continue;
        }
        std::size_t d = i + 2;
        while (d < n && d - (i + 2) < 9 && std::isdigit(static_cast<unsigned char>(syncReply[d])) != 0) {
            ++d;
        }
        const std::size_t digits = d - (i + 2);
        if (digits < 6 || digits > 8 || d >= n || (syncReply[d] != '-' && syncReply[d] != '_')) {
            continue;
        }
        std::size_t e = d;
        while (e < n && e - d < 80 && nameChar(syncReply[e])) {
            ++e;
        }
        std::string tail(syncReply.substr(d, e - d));
        for (auto& c : tail) {
            c = lower(c);
        }
        if (!tail.ends_with(".msu") && !tail.ends_with(".cab")) {
            continue;
        }
        std::wstring kb = L"KB" + utf8::toWide(syncReply.substr(i + 2, digits));
        if (seen.insert(kb).second) {
            out.push_back(std::move(kb));
        }
        i = e;
    }
    return out;
}

Result<std::vector<std::wstring>> discoverWindowsUpdateKbs(const WuQuery& query, const CancelToken& cancel) {
    const std::wstring branchW = wuBranch(query.build);
    if (branchW.empty()) {
        return fail(ErrorCode::Unsupported, L"Windows Update has no branch name for this build", std::to_wstring(query.build));
    }
    const std::string branch = utf8::fromWide(branchW);
    const std::string arch = utf8::fromWide(query.architecture);
    std::string archUpper = arch;
    for (auto& c : archUpper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const std::string edition = utf8::fromWide(query.edition);
    // Asked as the build's first release (UBR 1): everything newer than it is on offer.
    const std::string version = std::format("10.0.{}.1", query.build);

    const std::string cookieBody = std::format(
        R"(<GetCookie xmlns="{}"><oldCookie><Expiration>{}</Expiration></oldCookie><lastChange>{}</lastChange><currentTime>{}</currentTime>)"
        R"(<protocolVersion>2.0</protocolVersion></GetCookie>)",
        kNs, utcNow(), utcNow(), utcNow());
    auto cookieReply = post("GetCookie", cookieBody, cancel);
    if (!cookieReply) {
        return std::unexpected(cookieReply.error());
    }
    const std::string cookie = between(*cookieReply, "<EncryptedData>", "</EncryptedData>");
    if (cookie.empty()) {
        return fail(ErrorCode::IoError, L"Windows Update gave no cookie");
    }

    const std::string osProduct = std::format("PN=Client.OS.rs2.{}&amp;Branch={}&amp;PrimaryOSProduct=1&amp;Repairable=1&amp;V={}&amp;ReofferUpdate=1",
                                              arch, branch, version);
    const std::string wuOs = std::format(
        "E:App=WU_OS&amp;AppVer={0}&amp;BlockFeatureUpdates=1&amp;BranchReadinessLevel=CB&amp;CurrentBranch={1}&amp;DeviceFamily=Windows.Desktop"
        "&amp;FlightingBranchName=&amp;FlightRing=Retail&amp;InstallationType=Client&amp;IsFlightingEnabled=0&amp;IsRetailOS=1"
        "&amp;OSArchitecture={2}&amp;OSSkuId={3}&amp;OSVersion={0}&amp;EditionId={4}&amp;ProductType=WinNT&amp;ReleaseType=Production&amp;WuClientVer={0}",
        version, branch, archUpper, query.sku, edition);
    const std::string setup = std::format(
        "E:App=Setup360&amp;AppVer=10.0&amp;AttrDataVer=331&amp;BuildFlighting=0&amp;CurrentBranch={1}&amp;DUScan=1&amp;DUInternal=0"
        "&amp;FlightRing=Retail&amp;InstallationType=Client&amp;IsRetailOS=1&amp;MediaBranch={1}&amp;MediaVersion={0}&amp;OSArchitecture={2}"
        "&amp;OSSkuId={3}&amp;EditionId={4}&amp;CompositionEditionId={4}&amp;OSVersion={0}&amp;LCUVersion={0}&amp;ProductType=WinNT"
        "&amp;ReleaseType=Production&amp;IsoCountryShortCode=US&amp;HotPatchEligible=0",
        version, branch, archUpper, query.sku, edition);
    const std::string netfx = std::format("PN=Microsoft.NETFX.{}&amp;V=2018.12.2.0", arch);

    std::vector<std::wstring> kbs;
    std::set<std::wstring> seen;
    std::optional<Error> lastError;
    std::size_t answered = 0;
    for (const auto& [device, products, what] : {std::tuple{wuOs, osProduct, L"WU_OS"}, std::tuple{setup, osProduct, L"Setup360"},
                                                 std::tuple{wuOs, netfx, L"NETFX"}}) {
        if (auto go = cancel.check(); !go) {
            return std::unexpected(go.error());
        }
        Error error;
        const std::string reply = sync(cookie, device, products, cancel, error);
        if (reply.empty()) {
            log::warn("updates", std::format(L"Windows Update ({}): {}", what, describe(error)));
            lastError = error;
            continue;
        }
        ++answered;
        const auto found = wuPackageKbs(reply);
        log::info("updates", std::format(L"Windows Update ({}): {} package KB(s)", what, found.size()));
        for (const auto& kb : found) {
            if (seen.insert(kb).second) {
                kbs.push_back(kb);
            }
        }
    }
    if (answered == 0 && lastError) {
        return std::unexpected(*lastError);
    }
    return kbs;
}

} // namespace wl::core
