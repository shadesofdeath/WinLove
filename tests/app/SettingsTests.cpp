// P16: settings.json round trip, defaults for missing / wrong fields, AppState notification.
#include "app/state/AppState.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"settings";
    std::filesystem::create_directories(dir);
    return dir / name;
}

void write(const std::filesystem::path& file, const char* text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

} // namespace

TEST_CASE("settings: every field survives save / load") {
    AppSettings settings;
    settings.theme = ThemeChoice::System;
    settings.reduceMotion = true;
    settings.language = Language::English;
    settings.workRoot = L"D:\\WinLove \u00e7al\u0131\u015fma";
    settings.mountFolder = L"E:\\mnt";
    settings.isoFolder = L"D:\\ISO";
    const auto file = scratch(L"roundtrip.json");
    settings.save(file);
    CHECK(AppSettings::load(file) == settings);
    CHECK(settings.mountDirectory() == std::filesystem::path(L"E:\\mnt"));

    settings.mountFolder.clear(); // empty: inside the work folder
    CHECK(settings.mountDirectory() == settings.workRoot / L"mount");
}

TEST_CASE("settings: a file from an older build, or a damaged one, falls back field by field") {
    const AppSettings defaults;
    const auto file = scratch(L"old.json");
    write(file, R"({"version":1,"workRoot":"D:\\Old","isoFolder":"D:\\ISO"})"); // before P16
    auto loaded = AppSettings::load(file);
    CHECK(loaded.workRoot == std::filesystem::path(L"D:\\Old"));
    CHECK(loaded.theme == ThemeChoice::Dark);
    CHECK(loaded.language == Language::Turkish);
    CHECK_FALSE(loaded.reduceMotion);
    CHECK(loaded.mountFolder.empty());

    write(file, R"({"theme":7,"language":"xx","reduceMotion":"yes","workRoot":5,"mountFolder":null})");
    loaded = AppSettings::load(file);
    CHECK(loaded == defaults);
    write(file, "not json at all");
    CHECK(AppSettings::load(file) == defaults);
    CHECK(AppSettings::load(scratch(L"missing.json")) == defaults);

    for (const auto& [name, theme] : {std::pair{"light", ThemeChoice::Light}, std::pair{"hc", ThemeChoice::HighContrast},
                                      std::pair{"system", ThemeChoice::System}, std::pair{"dark", ThemeChoice::Dark}}) {
        write(file, (std::string(R"({"theme":")") + name + "\"}").c_str());
        CHECK(AppSettings::load(file).theme == theme);
    }
}

TEST_CASE("settings: AppState saves a change and tells its listeners once") {
    const auto file = scratch(L"state.json");
    std::error_code ec;
    std::filesystem::remove(file, ec);
    AppState state{scratch(L"recent.json"), file};
    int notified = 0;
    state.subscribe([&](AppState::Change change) { notified += change == AppState::Change::Settings ? 1 : 0; });

    AppSettings next = state.settings();
    next.theme = ThemeChoice::Light;
    next.language = Language::English;
    state.setSettings(next);
    CHECK(notified == 1);
    CHECK(AppSettings::load(file) == next); // on disk right away
    state.setSettings(next);                // nothing changed: no write, no notification
    CHECK(notified == 1);
}
