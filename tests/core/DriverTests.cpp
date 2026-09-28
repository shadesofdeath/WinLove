// P09: INF [Version] / [Strings] / [Manufacturer] parsing.
#include "core/image/DriverInf.h"

#include <doctest.h>

using namespace wl;

TEST_CASE("parseInfText: class, provider via %strings%, DriverVer, catalog, architectures") {
    const std::wstring text = LR"(; Intel network adapter
[Version]
Signature   = "$WINDOWS NT$"
Class       = Net
ClassGUID   = {4d36e972-e325-11ce-bfc1-08002be10318}
Provider    = %Intel%   ; vendor
CatalogFile = e2f68.cat
DriverVer   = 07/18/2024,2.1.4.3

[Manufacturer]
%Intel%     = Intel, NTamd64.10.0...16299, NTarm64

[Strings]
Intel = "Intel Corporation"
)";
    const auto inf = core::parseInfText(text, LR"(C:\d\e2f68.inf)");
    CHECK(inf.className == L"Net");
    CHECK(inf.classGuid == L"{4d36e972-e325-11ce-bfc1-08002be10318}");
    CHECK(inf.provider == L"Intel Corporation");
    CHECK(inf.catalog == L"e2f68.cat");
    CHECK(inf.date == L"07/18/2024");
    CHECK(inf.version == L"2.1.4.3");
    REQUIRE(inf.architectures.size() == 2);
    CHECK(inf.supports(L"x64"));
    CHECK(inf.supports(L"arm64"));
    CHECK_FALSE(inf.supports(L"x86"));
}

TEST_CASE("parseInfText: undecorated models support every architecture; missing fields stay empty") {
    const auto inf = core::parseInfText(L"[Version]\nClass=System\n[Manufacturer]\n%M%=Models\n");
    CHECK(inf.className == L"System");
    CHECK(inf.provider.empty());
    CHECK(inf.architectures.empty());
    CHECK(inf.supports(L"x86"));
}
