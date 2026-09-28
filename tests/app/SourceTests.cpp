// P01 Source page: formatting, recent list persistence, and the open-source flow end to end
// (Shell → engine thread → UI thread → AppState → Images page) against the real ISO.
#include "app/Format.h"
#include "app/shell/Shell.h"
#include "app/state/AppState.h"
#include "base/Utf8.h"
#include "support/TestGraphics.h"
#include "ui/widget/Host.h"

#include <doctest.h>
#include <json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using namespace std::chrono_literals;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

const Localization& turkish() {
    static const Localization loc = Localization::fromJson(
                                        readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json"))
                                        .value();
    return loc;
}

std::filesystem::path tempFile(const wchar_t* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

core::SourceInfo fakeSource(const wchar_t* path, int build) {
    core::SourceInfo info;
    info.path = path;
    info.format = core::ImageFormat::Wim;
    core::ImageInfo image;
    image.build = build;
    image.spBuild = 1;
    image.architecture = core::Architecture::X64;
    info.install.images.push_back(image);
    return info;
}

std::filesystem::path isoPath() {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"tests/integration/fixtures/win11_25h2_tr.json");
    return utf8::toWide(nlohmann::json::parse(file)["iso"].get<std::string>());
}

bool haveIso() {
    std::error_code ec;
    return std::filesystem::exists(isoPath(), ec);
}

} // namespace

TEST_CASE("formatBytes uses the UI language's decimal separator") {
    CHECK(formatBytes(7217280579ull, Language::Turkish) == L"6,72 GB");
    CHECK(formatBytes(7217280579ull, Language::English) == L"6.72 GB");
    CHECK(formatBytes(512, Language::English) == L"512 B");
    CHECK(formatBytes(1536, Language::Turkish) == L"1,50 KB");
}

TEST_CASE("formatRecentTime: today, days ago, older dates") {
    const auto now = std::chrono::system_clock::now();
    CHECK(formatRecentTime(now - 1min, Language::Turkish, turkish(), now).starts_with(L"bugün "));
    CHECK(formatRecentTime(now - 72h, Language::Turkish, turkish(), now) == L"3 gün önce");
    const auto older = formatRecentTime(now - 24h * 40, Language::Turkish, turkish(), now);
    CHECK_FALSE(older.empty());
    CHECK(older.find(L"önce") == std::wstring::npos);
}

TEST_CASE("RecentSources: newest first, de-duplicated, capped at 10, persisted") {
    const auto file = tempFile(L"winlove-test-recent.json");
    {
        RecentSources recent(file);
        recent.load(); // missing file = empty
        CHECK(recent.entries().empty());
        for (int i = 0; i < 12; ++i) {
            recent.touch(fakeSource((L"C:\\img\\" + std::to_wstring(i) + L".wim").c_str(), 26200));
        }
        recent.touch(fakeSource(L"c:\\IMG\\5.wim", 26100)); // same file, different case: moves to top
        REQUIRE(recent.entries().size() == RecentSources::kCapacity);
        CHECK(recent.entries().front().summary == L"11 24H2 · 26100.1");
        CHECK(recent.entries()[1].path == L"C:\\img\\11.wim");
    }
    RecentSources reloaded(file);
    reloaded.load();
    REQUIRE(reloaded.entries().size() == RecentSources::kCapacity);
    CHECK(reloaded.entries().front().summary == L"11 24H2 · 26100.1");
    reloaded.remove(L"C:\\img\\11.wim");
    CHECK(reloaded.entries().size() == RecentSources::kCapacity - 1);
    std::filesystem::remove(file);
}

TEST_CASE("RecentSources: a corrupt file is treated as empty, not fatal") {
    const auto file = tempFile(L"winlove-test-recent-bad.json");
    std::ofstream(file) << "{ not json";
    RecentSources recent(file);
    recent.load();
    CHECK(recent.entries().empty());
    std::filesystem::remove(file);
}

TEST_CASE("Source flow: opening the ISO fills AppState and switches to Images" * doctest::skip(!haveIso())) {
    const auto recentFile = tempFile(L"winlove-test-recent-flow.json");
    AppState state(recentFile);
    std::vector<std::function<void()>> posted;
    Shell::Services services;
    services.minimize = services.toggleMaximize = services.close = services.toggleTheme = [] {};
    services.postToUi = [&](std::function<void()> fn) { posted.push_back(std::move(fn)); };

    ui::Host host(ui::HostServices{[] {}, nullptr, nullptr, test::graphics().text.get()});
    auto shell = std::make_unique<Shell>(turkish(), Language::Turkish, state, services);
    Shell* raw = shell.get();
    host.setRoot(std::move(shell));
    host.layout({1440, 900});

    raw->openSource(isoPath());
    state.engine().drain();          // engine thread finished openSource
    REQUIRE(posted.size() == 1);     // result handed to the "UI thread"
    posted.front()();                // run it as the message loop would

    REQUIRE(state.source().has_value());
    CHECK(state.source()->install.images.size() == 6);
    CHECK(raw->currentPage() == PageId::Images);
    REQUIRE(state.recent().entries().size() == 1);
    CHECK(state.recent().entries().front().format == L"ISO");
    CHECK(state.recent().entries().front().summary == L"11 25H2 · 26200.8037");

    // A bad file reports on the Source page and does not change the current source.
    raw->showPage(PageId::Source);
    raw->openSource(L"C:\\does\\not\\exist.iso");
    state.engine().drain();
    REQUIRE(posted.size() == 2);
    posted.back()();
    CHECK(raw->currentPage() == PageId::Source);
    CHECK(state.source()->install.images.size() == 6);
    std::filesystem::remove(recentFile);
}

TEST_CASE("Shell drag rules: source types accepted, anything else refused") {
    const auto recentFile = tempFile(L"winlove-test-recent-drag.json");
    AppState state(recentFile);
    Shell::Services services;
    services.postToUi = [](std::function<void()> fn) { fn(); };
    Shell shell(turkish(), Language::Turkish, state, services);
    CHECK(shell.dragEnter({L"C:\\x\\Win.iso"}));
    CHECK(shell.dragEnter({L"C:\\x\\install.ESD"}));
    CHECK_FALSE(shell.dragEnter({L"C:\\x\\notes.txt"}));
    CHECK_FALSE(shell.dragEnter({}));
    std::filesystem::remove(recentFile);
}
