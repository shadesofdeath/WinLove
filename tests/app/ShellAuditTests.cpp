// docs/AUDIT-2026-11-10.md, src/app (state, report, recent list): the behaviour its fixes changed, pinned.
#include "app/ApplyReport.h"
#include "app/state/AppState.h"
#include "app/state/RecentSources.h"

#include <doctest.h>

#include <chrono>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"shell-audit";
    std::filesystem::create_directories(dir);
    const auto file = dir / name;
    std::error_code ec;
    std::filesystem::remove(file, ec);
    return file;
}

core::DriverInf inf(const wchar_t* path) {
    core::DriverInf d;
    d.path = path;
    return d;
}

} // namespace

TEST_CASE("audit APP-08: a driver scan lists each INF once, whatever its spelling") {
    AppState state{scratch(L"recent-drivers.json"), scratch(L"settings-drivers.json")};
    int told = 0;
    const auto id = state.subscribe([&](AppState::Change change) { told += change == AppState::Change::Drivers ? 1 : 0; });
    state.addDriverScan(LR"(X:\drivers)", {inf(LR"(X:\drivers\net\a.inf)"), inf(LR"(X:\drivers\net\b.inf)"),
                                           inf(LR"(X:\drivers\net\a.inf)")});
    CHECK(state.driverScan().infs.size() == 2);
    CHECK(state.driverScan().folders.size() == 1);
    // The same folder again, and the same files under another spelling: nothing new.
    state.addDriverScan(LR"(X:\drivers)", {inf(LR"(X:\DRIVERS\Net\A.INF)"), inf(LR"(X:\drivers\net\.\b.inf)"),
                                           inf(LR"(X:\drivers\disk\c.inf)")});
    CHECK(state.driverScan().infs.size() == 3);
    CHECK(state.driverScan().folders.size() == 1);
    CHECK(state.driverScan().infs[0].path == std::filesystem::path(LR"(X:\drivers\net\a.inf)")); // the first spelling stays
    CHECK(state.driverScan().infs[2].path == std::filesystem::path(LR"(X:\drivers\disk\c.inf)"));
    CHECK(told == 2);
    state.unsubscribe(id);
}

TEST_CASE("audit APP-16: a listener unsubscribed during a notification is not called in it") {
    AppState state{scratch(L"recent-listeners.json"), scratch(L"settings-listeners.json")};
    std::vector<int> calls;
    std::size_t second = 0;
    std::size_t self = 0;
    // The first destroys what the second belongs to (a page switch inside a notification).
    const auto first = state.subscribe([&](AppState::Change) {
        calls.push_back(1);
        state.unsubscribe(second);
    });
    second = state.subscribe([&](AppState::Change) { calls.push_back(2); });
    // One that takes itself out while it runs must stay alive until it returns.
    self = state.subscribe([&, keep = std::make_shared<int>(3)](AppState::Change) {
        state.unsubscribe(self);
        calls.push_back(*keep);
    });
    const auto last = state.subscribe([&](AppState::Change) { calls.push_back(4); });

    state.setMountFolder(std::nullopt);
    CHECK(calls == std::vector<int>{1, 3, 4});
    calls.clear();
    state.setMountFolder(std::nullopt);
    CHECK(calls == std::vector<int>{1, 4});

    // One subscribed during a notification hears the next one, not this one.
    calls.clear();
    std::size_t late = 0;
    const auto adder = state.subscribe([&](AppState::Change) {
        if (late == 0) {
            late = state.subscribe([&](AppState::Change) { calls.push_back(5); });
        }
    });
    state.setMountFolder(std::nullopt);
    CHECK(calls == std::vector<int>{1, 4});
    calls.clear();
    state.setMountFolder(std::nullopt);
    CHECK(calls == std::vector<int>{1, 4, 5});
    for (const auto id : {first, last, adder, late}) {
        state.unsubscribe(id);
    }
}

TEST_CASE("audit APP-03: held downloads live in the state, not in the shell that is rebuilt") {
    AppState state{scratch(L"recent-held.json"), scratch(L"settings-held.json")};
    CHECK(state.heldDownloads().count() == 0);
    state.heldDownloads().source = LR"(X:\win.iso)";
    state.heldDownloads().updates.push_back(LR"(X:\kb1.msu)");
    state.heldDownloads().languages.push_back(LR"(X:\lp.cab)");
    state.heldDownloads().apps.push_back(LR"(X:\app.msixbundle)");
    CHECK(state.heldDownloads().count() == 3);
    state.heldDownloads() = {};
    CHECK(state.heldDownloads().count() == 0);
    CHECK(state.heldDownloads().source.empty());
}

TEST_CASE("audit APP-18: the report's file name is the local time, without the time zone database") {
    const auto now = std::chrono::system_clock::now();
    std::wstring name;
    CHECK_NOTHROW(name = applyReportFileName(now));
    // "WinLove-rapor-YYYYMMDD-HHMM.html"
    const std::wstring prefix = L"WinLove-rapor-";
    REQUIRE(name.size() == prefix.size() + 8 + 1 + 4 + 5);
    CHECK(name.starts_with(prefix));
    CHECK(name.ends_with(L".html"));
    CHECK(name[prefix.size() + 8] == L'-');
    for (std::size_t i = prefix.size(); i < name.size() - 5; ++i) {
        if (i != prefix.size() + 8) {
            CHECK(std::iswdigit(name[i]) != 0);
        }
    }
    // The fields are where the name says: a month and an hour that exist.
    const int month = std::stoi(name.substr(prefix.size() + 4, 2));
    const int hour = std::stoi(name.substr(prefix.size() + 9, 2));
    CHECK(month >= 1);
    CHECK(month <= 12);
    CHECK(hour <= 23);
}

TEST_CASE("audit APP-09: the recent list tells the same file by its spelling") {
    const auto file = scratch(L"recent-same.json");
    RecentSources recent{file};
    const auto dir = file.parent_path();
    core::SourceInfo source;
    source.format = core::ImageFormat::Wim;
    source.path = dir / L"Install.wim"; // no such file: only the spelling can tell
    recent.touch(source);
    source.path = dir / L"sub" / L".." / L"INSTALL.WIM";
    recent.touch(source);
    CHECK(recent.entries().size() == 1);
    source.path = dir / L"other.wim";
    recent.touch(source);
    CHECK(recent.entries().size() == 2);
    recent.remove(dir / L"install.wim");
    REQUIRE(recent.entries().size() == 1);
    CHECK(recent.entries().front().path == dir / L"other.wim");
    std::error_code ec;
    std::filesystem::remove(file, ec);
}
