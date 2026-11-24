// P16: settings.json round trip, defaults for missing / wrong fields, AppState notification.
#include "app/state/AppState.h"
#include "base/Text.h"
#include "core/image/WorkCopy.h"
#include "core/iso/IsoBuilder.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

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
    settings.accent = 0x7A3EC8; // D-143: any colour
    settings.reduceMotion = true;
    settings.language = Language::English;
    settings.workRoot = L"D:\\WinLove \u00e7al\u0131\u015fma";
    settings.mountFolder = L"E:\\mnt";
    settings.isoFolder = L"D:\\ISO";
    settings.pauseBeforeSave = true; // D-106
    settings.dismEngine = DismEngineChoice::Adk; // D-123
    settings.esdThreads = 6;
    settings.presetInIso = false;
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

    for (const auto& [name, theme] : {std::pair{"light", ThemeChoice::Light}, std::pair{"black", ThemeChoice::Black}, std::pair{"hc", ThemeChoice::Black},
                                      std::pair{"system", ThemeChoice::System}, std::pair{"dark", ThemeChoice::Dark}}) {
        write(file, (std::string(R"({"theme":")") + name + "\"}").c_str());
        CHECK(AppSettings::load(file).theme == theme);
    }
}

TEST_CASE("settings: D-143 — the accent is saved as #RRGGBB; older names and garbage load as colours") {
    const auto file = scratch(L"accent.json");
    AppSettings settings;
    settings.accent = 0x0F766E;
    settings.save(file);
    {
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"accent\": \"#0F766E\"") != std::string::npos);
    }
    CHECK(AppSettings::load(file).accent == 0x0F766Eu);
    // The preset names of the files before D-143 (with "design": 2, else D-107 resets the accent anyway).
    const std::pair<const char*, ui::Accent> names[] = {{"blue", ui::Accent::Blue}, {"copper", ui::Accent::Copper},
                                                        {"sea", ui::Accent::Sea}, {"pomegranate", ui::Accent::Pomegranate},
                                                        {"olive", ui::Accent::Olive}};
    for (const auto& [name, preset] : names) {
        CAPTURE(name);
        write(file, (std::string(R"({"design":2,"accent":")") + name + "\"}").c_str());
        CHECK(AppSettings::load(file).accent == ui::presetColor(preset));
    }
    for (const char* text : {R"({"design":2,"accent":"#7a3ec8"})", R"({"design":2,"accent":"7A3EC8"})"}) {
        write(file, text);
        CHECK(AppSettings::load(file).accent == 0x7A3EC8u);
    }
    // Unknown, damaged or wrongly typed: Mavi.
    for (const char* text : {R"({"design":2,"accent":"sky"})", R"({"design":2,"accent":"#12345"})", R"({"design":2,"accent":"çok mavi"})",
                             R"({"design":2,"accent":12})", R"({"design":2})"}) {
        CAPTURE(text);
        write(file, text);
        CHECK(AppSettings::load(file).accent == ui::kDefaultAccent);
    }
    // Before D-107 ("design" < 2): Mavi whatever was chosen.
    write(file, R"({"accent":"#7A3EC8"})");
    CHECK(AppSettings::load(file).accent == ui::kDefaultAccent);
    CHECK(accentFromText("") == ui::kDefaultAccent);
    CHECK(accentFromText("#ABC") == 0xAABBCCu);
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

TEST_CASE("settings: only folders under the work folder count as WinLove's own copies") {
    AppSettings settings;
    settings.workRoot = L"D:\\Data\\WinLove";
    CHECK(settings.isWorkCopy(L"D:\\Data\\WinLove\\work\\Win11_25H2"));
    CHECK(settings.isWorkCopy(L"d:\\data\\winlove\\WORK\\Win11_25H2\\sources"));
    CHECK(settings.isWorkCopy(L"C:\\WinLove\\work\\Win11_25H2_Turkish_x64_v2")); // builds before 2026-09-28
    CHECK_FALSE(settings.isWorkCopy(L"D:\\Data\\WinLove\\work"));                 // the work folder itself
    CHECK_FALSE(settings.isWorkCopy(L"D:\\Data\\WinLove\\workshop\\x"));
    CHECK_FALSE(settings.isWorkCopy(L"D:\\Data\\WinLove\\mount"));
    CHECK_FALSE(settings.isWorkCopy(L"C:\\Users\\me\\Downloads\\Win11.iso"));
    CHECK_FALSE(settings.isWorkCopy(L"D:\\Data\\WinLove\\work\\..\\..\\Documents"));
    CHECK_FALSE(settings.isWorkCopy(L"work\\Win11"));
}

TEST_CASE("settings: a source's work folder leaves the setup media's deepest file under 260 characters (report 2026-11-24)") {
    const std::filesystem::path root = L"C:\\Users\\Teknik\\AppData\\Local\\WinLove\\work";
    // A usual name stays as it is.
    CHECK(AppSettings::workFolderFor(root, L"D:\\iso\\Win11_25H2_Turkish_x64_v2.iso") == root / L"Win11_25H2_Turkish_x64_v2");
    // A UUP / official ISO name is cut and tagged with a hash of the whole name.
    const std::wstring uup = L"26200.9818.261025-0959.25H2_GE_RELEASE_SVC_PROD3_CLIENTMULTI_X64FRE_TR-TR";
    const auto cut = AppSettings::workFolderFor(root, L"C:\\Users\\Teknik\\Downloads\\" + uup + L".iso");
    CHECK(cut.parent_path() == root);
    const std::wstring name = cut.filename().wstring();
    CHECK(name.size() < uup.size());
    CHECK(uup.starts_with(name.substr(0, name.find(L'~'))));
    CHECK(name.find(L'~') != std::wstring::npos);
    const std::wstring deepest =
        L"\\sources\\replacementmanifests\\microsoft-windows-appx-deployment-server\\"
        L"microsoft.windows.secondarytileexperience_10.0.0.0_neutral__cw5n1h2txyewy.xml.partial";
    CHECK(cut.wstring().size() + deepest.size() <= 259);
    // The same name gives the same folder (the tag ignores case); another long name another one.
    CHECK(AppSettings::workFolderFor(root, L"E:\\" + uup + L".ISO") == cut);
    const std::wstring lowered = AppSettings::workFolderFor(root, text::lower(uup) + L".iso").filename().wstring();
    CHECK(lowered.substr(lowered.find(L'~')) == name.substr(name.find(L'~')));
    const std::wstring other = L"26200.9818.261025-0959.25H2_GE_RELEASE_SVC_PROD3_CLIENTMULTI_X64FRE_EN-US";
    CHECK(AppSettings::workFolderFor(root, other + L".iso") != cut);
    // Still a work copy WinLove may delete.
    AppSettings settings;
    settings.workRoot = root.parent_path();
    CHECK(settings.isWorkCopy(cut));
}

TEST_CASE("work copy: an ISO with a UUP-long name extracts, its deepest setup file included (report 2026-11-24)") {
    // wl_tests.exe has no longPathAware manifest: past 259 characters fails here as it does in the
    // app on a PC with LongPathsEnabled off, whatever this PC says.
    using namespace wl::core;
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"long-name";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const std::wstring deep = L"sources\\replacementmanifests\\microsoft-windows-appx-deployment-server\\"
                              L"microsoft.windows.secondarytileexperience_10.0.0.0_neutral__cw5n1h2txyewy.xml";
    const auto put = [](const std::filesystem::path& file, const std::string& content) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << content;
    };
    const auto media = dir / L"media";
    REQUIRE((media / deep).wstring().size() <= 259); // the test's own media must be writable here
    put(media / L"boot" / L"etfsboot.com", std::string(4096, '\0'));
    put(media / L"sources" / L"install.wim", "image");
    put(media / deep, "<manifest/>");
    const std::wstring uup = L"26200.9818.261025-0959.25H2_GE_RELEASE_SVC_PROD3_CLIENTMULTI_X64FRE_TR-TR";
    const auto iso = dir / (uup + L".iso");
    IsoOptions options;
    options.sourceFolder = media;
    options.output = iso;
    options.volumeLabel = L"WL_TEST";
    options.boot = BootMode::BiosOnly;
    REQUIRE(buildIso(options, TaskContext{}).has_value());
    const auto root = dir / L"work";

    // The folder named after the whole file name (as before): its deepest file does not fit.
    const auto whole = root / uup;
    REQUIRE(whole.wstring().size() + 1 + deep.size() + 8 > 259);
    const auto refused = extractWorkCopy(iso, whole, /*fresh=*/false, TaskContext{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().hresult != 0); // Windows' own reason, no more 0x00000000
    CHECK(refused.error().message.find(L"characters long") != std::wstring::npos);

    // The folder the app names now: everything comes out.
    const auto folder = AppSettings::workFolderFor(root, iso);
    const auto extracted = extractWorkCopy(iso, folder, /*fresh=*/false, TaskContext{});
    REQUIRE_MESSAGE(extracted.has_value(), describe(extracted.error()).c_str());
    CHECK(std::filesystem::exists(folder / deep));
    CHECK(inspectWorkCopy(iso, folder) == WorkCopyState::Pristine);
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("audit APP-06: wrongly typed Windows-download settings are skipped, not thrown out of startup") {
    const AppSettings defaults;
    const auto file = scratch(L"typed.json");
    write(file, R"({"windowsDownload":{"updates":1,"edge":"no","apps":null,"netFx3":[],"resetBase":{},"esd":0.5}})");
    AppSettings loaded;
    CHECK_NOTHROW(loaded = AppSettings::load(file));
    CHECK(loaded.windowsDownload.updates == defaults.windowsDownload.updates);
    CHECK(loaded.windowsDownload.esd == defaults.windowsDownload.esd);
    write(file, R"({"windowsDownload":{"esd":true,"updates":false}})");
    loaded = AppSettings::load(file);
    CHECK(loaded.windowsDownload.esd);
    CHECK_FALSE(loaded.windowsDownload.updates);
}
