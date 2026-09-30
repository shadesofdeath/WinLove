// P18: search folding, match ranks, and what the palette index finds in which state.
#include "app/shell/CommandPalette.h"
#include "app/shell/PaletteIndex.h"
#include "support/TestGraphics.h"
#include "ui/widget/Host.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::StartType;
using Kind = PaletteItem::Kind;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string resource(const wchar_t* relative) {
    return readFile(std::filesystem::path(WL_SOURCE_DIR) / relative);
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"palette";
    std::filesystem::create_directories(dir);
    return dir / name;
}

core::AppxComponent app(const wchar_t* identity, std::uint64_t mb) {
    core::AppxComponent c;
    c.package.displayName = identity;
    c.package.packageName = std::wstring(identity) + L"_1.0.0.0_neutral_~_8wekyb3d8bbwe";
    c.size = mb * 1024 * 1024;
    return c;
}

core::ServiceEntry service(const wchar_t* name, const wchar_t* display, StartType start) {
    core::ServiceEntry s;
    s.name = name;
    s.displayName = display;
    s.start = start;
    s.type = 0x10;
    return s;
}

core::OptionalFeature feature(const wchar_t* name, const wchar_t* display) {
    return {core::OptionalFeature::Kind::Feature, name, display, {}, core::ServicingState::Installed, 0, false};
}

struct Fixture {
    Localization strings = *Localization::fromJson(resource(L"resources/strings/tr.json"));
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    std::function<void(std::function<void()>)> post = [](std::function<void()> fn) { fn(); };
    ImageSettingsController settings{state, *ImageSettingsCatalog::parse(resource(L"resources/catalog/settings.json"))};
    ComponentController components{state, *AppxCatalog::parse(resource(L"resources/catalog/appx.json")), Language::Turkish,
                                   post};
    FeatureController features{state, post};
    ServiceController services{state, R"({"format":"winlove.catalog.services","services":[]})", post};
    std::set<PaletteCommand> unavailable;
    bool collapsed = false;
    PaletteIndex index{PaletteIndex::Sources{
        state, strings, Language::Turkish, settings, components, features, services,
        [this](PaletteCommand c) { return !unavailable.contains(c); },
        [this] { return collapsed; },
    }};

    void mount() {
        state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    }
    void lists() {
        state.setAppxList(AppState::AppxList{
            AppState::AppxList::Status::Ready, L"C:\\m",
            {app(L"Microsoft.XboxGamingOverlay", 96), app(L"Microsoft.XboxIdentityProvider", 8),
             app(L"Microsoft.ZuneMusic", 142), app(L"Contoso.Unknown", 3)},
            {}});
        state.setOptionalFeatures(AppState::OptionalFeatures{
            AppState::OptionalFeatures::Status::Ready, L"C:\\m",
            {feature(L"Microsoft-Windows-Subsystem-Linux", L"Linux için Windows Alt Sistemi"),
             feature(L"Printing-XPSServices-Features", L"Microsoft XPS Belge Yazıcısı")},
            {}});
        state.setServiceList(AppState::ServiceList{
            AppState::ServiceList::Status::Ready, L"C:\\m",
            {service(L"XblAuthManager", L"Xbox Live Kimlik Doğrulama", StartType::Manual),
             service(L"DiagTrack", L"Bağlı Kullanıcı Deneyimleri ve Telemetri", StartType::Auto),
             service(L"Spooler", L"Spooler", StartType::Auto)},
            {}});
    }
    const PaletteItem* find(const std::vector<PaletteItem>& items, Kind kind, std::wstring_view id) const {
        for (const auto& item : items) {
            if (item.kind == kind && item.id == id) {
                return &item;
            }
        }
        return nullptr;
    }
};

bool hasCommand(const PaletteResults& found, PaletteCommand command) {
    return std::ranges::any_of(found.commands, [&](const PaletteItem& i) { return i.command == command; });
}

} // namespace

TEST_CASE("palette folding: one character per character, Turkish letters find their plain spelling") {
    CHECK(foldForSearch(L"WINDOWS") == L"windows");
    CHECK(foldForSearch(L"wındows") == L"windows");
    CHECK(foldForSearch(L"İŞIK") == L"isik");
    CHECK(foldForSearch(L"Güncellemeler") == L"guncellemeler");
    CHECK(foldForSearch(L"Çöğüş — Ünite").size() == std::wstring(L"Çöğüş — Ünite").size());
    CHECK(foldForSearch(L"").empty());
}

TEST_CASE("palette match: start of the name, start of a word, inside a word, words apart, extra text") {
    CHECK(matchPalette(L"Defender", {}, L"def").rank == 0);
    // The best placed occurrence wins, and the mark sits on it.
    const auto word = matchPalette(L"WinDefend — Microsoft Defender Antivirus Service", {}, L"DEF");
    CHECK(word.rank == 1);
    CHECK(word.at == 22);
    CHECK(word.length == 3);
    CHECK(matchPalette(L"WinDefend", {}, L"def").rank == 2);
    CHECK(matchPalette(L"Defender bildirimlerini kapat", {}, L"kapat defender").rank == 3);
    const auto extra = matchPalette(L"Xbox Game Bar", L"Microsoft.XboxGamingOverlay Oyun", L"overlay");
    CHECK(extra.rank == 4);
    CHECK(extra.length == 0);
    CHECK(matchPalette(L"Xbox Game Bar", L"Microsoft.XboxGamingOverlay", L"xbox overlay").rank == 4);
    CHECK_FALSE(matchPalette(L"Defender", L"Windows-Defender", L"edge"));
    CHECK_FALSE(matchPalette(L"Defender", {}, L"   "));
    CHECK(matchPalette(L"Güncellemeler", {}, L"  GUNCEL ").rank == 0);
}

TEST_CASE("palette: an empty query lists the commands that can run now, and nothing else") {
    Fixture f;
    f.unavailable = {PaletteCommand::ApplyQueue, PaletteCommand::Unmount};
    const auto idle = f.index.search(L"");
    CHECK(idle.results.empty());
    CHECK(idle.total == 0);
    CHECK_FALSE(hasCommand(idle, PaletteCommand::ApplyQueue));
    CHECK_FALSE(hasCommand(idle, PaletteCommand::Unmount));
    CHECK(hasCommand(idle, PaletteCommand::ToggleTheme));
    CHECK(hasCommand(idle, PaletteCommand::OpenLogFolder));
    CHECK(idle.commands.size() == 6);

    // The rail command says what it will do.
    const auto nav = [&] {
        const auto found = f.index.search(L"navigasyon");
        REQUIRE(found.commands.size() == 1);
        return found.commands.front();
    };
    CHECK(nav().name == f.strings.get(Str::PaletteCmdCollapseNav));
    CHECK(nav().keys == std::vector<std::wstring>{L"Ctrl B"});
    f.collapsed = true;
    CHECK(nav().name == f.strings.get(Str::PaletteCmdExpandNav));
}

TEST_CASE("palette: pages are always found; the image's own things only once it is mounted and read") {
    Fixture f;
    auto found = f.index.search(L"serv");
    REQUIRE_FALSE(found.results.empty());
    CHECK(found.results.front().kind == Kind::Page);
    CHECK(found.results.front().page == PageId::Services);
    CHECK(std::ranges::all_of(found.results, [](const PaletteItem& i) { return i.kind == Kind::Page; }));
    // By page key and description too, and never the developer gallery.
    CHECK(f.index.search(L"postsetup").results.front().page == PageId::PostSetup);
    CHECK(f.index.search(L"dosya kopyalama").results.front().page == PageId::PostSetup);
    CHECK(f.index.search(L"gallery").results.empty());
    CHECK(f.index.search(L"xbox").results.empty());

    f.mount();
    found = f.index.search(L"xbox");
    // Mounted, lists not read yet: no app — only the form's own "Xbox Game Bar" setting.
    CHECK(std::ranges::all_of(found.results, [](const PaletteItem& i) { return i.kind == Kind::Setting; }));
    // The settings form needs no list.
    const auto& first = f.settings.catalog().settings().front();
    found = f.index.search(first.label.tr);
    REQUIRE_FALSE(found.results.empty());
    CHECK(found.results.front().kind == Kind::Setting);
    CHECK(found.results.front().id == std::wstring(first.id.begin(), first.id.end()));
    CHECK(found.results.front().detail.starts_with(f.strings.get(Str::NavTweaks)));

    f.lists();
    found = f.index.search(L"xbox");
    CHECK(found.total == found.results.size());
    const auto* overlay = f.find(found.results, Kind::Component, L"Microsoft.XboxGamingOverlay_1.0.0.0_neutral_~_8wekyb3d8bbwe");
    REQUIRE(overlay);
    CHECK(overlay->page == PageId::Components);
    CHECK(overlay->detail.find(L"96") != std::wstring::npos);
    const auto* xbl = f.find(found.results, Kind::Service, L"XblAuthManager");
    REQUIRE(xbl);
    CHECK(xbl->name == L"XblAuthManager — Xbox Live Kimlik Doğrulama");
    CHECK(xbl->detail == f.strings.get(Str::NavServices) + L" · " + f.strings.get(Str::ServicesStartManual));
    // A service whose display name is its name is not written twice; features by technical name.
    CHECK(f.index.search(L"spooler").results.front().name == L"Spooler");
    found = f.index.search(L"subsystem-linux");
    REQUIRE(found.results.size() == 1);
    CHECK(found.results.front().kind == Kind::Feature);
    CHECK(found.results.front().name == L"Linux için Windows Alt Sistemi");
    CHECK(found.results.front().markLength == 0); // matched the technical name, nothing to mark
}

TEST_CASE("palette: details follow the queue; better matches first; the list is cut, the total is not") {
    Fixture f;
    f.mount();
    f.lists();
    const std::wstring package = L"Microsoft.ZuneMusic_1.0.0.0_neutral_~_8wekyb3d8bbwe";
    auto zune = [&] {
        const auto found = f.index.search(L"zunemusic");
        const auto* item = f.find(found.results, Kind::Component, package);
        REQUIRE(item);
        return *item;
    };
    CHECK(zune().detail.find(f.strings.get(Str::PaletteQueuedForRemoval)) == std::wstring::npos);
    for (const auto& group : f.components.groups()) {
        for (const auto& item : group.items) {
            if (item.packageName == package) {
                f.components.toggle(item);
            }
        }
    }
    CHECK(zune().detail.find(f.strings.get(Str::PaletteQueuedForRemoval)) != std::wstring::npos);

    f.services.set(f.state.serviceList()->items[1], StartType::Disabled);
    const auto diag = f.index.search(L"diagtrack");
    const auto* service = f.find(diag.results, Kind::Service, L"DiagTrack");
    REQUIRE(service);
    CHECK(service->detail.find(f.strings.get(Str::ServicesStartDisabled)) != std::wstring::npos);
    CHECK(service->detail.ends_with(f.strings.get(Str::ComponentsQueued)));

    // "a" is in nearly everything: ranks never go down along the list, and the cut keeps the count.
    const auto many = f.index.search(L"a");
    CHECK(many.results.size() == PaletteIndex::kMaxResults);
    CHECK(many.total > many.results.size());
    int last = 0;
    for (const auto& item : many.results) {
        const int rank = matchPalette(item.name, {}, L"a").rank;
        CHECK(rank >= last);
        last = rank;
    }
}

namespace {

// The palette as the shell shows it: a modal over an empty root, fed by a fixed list.
struct PaletteHarness {
    std::unique_ptr<ui::Host> host;
    CommandPalette* palette = nullptr;
    std::vector<std::wstring> events; // "close", "run:<name>"

    PaletteHarness() {
        host = std::make_unique<ui::Host>(ui::HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {},
                                                           test::graphics().text.get()});
        host->setRoot(std::make_unique<ui::Widget>());
        host->layout({1000, 700});
        open();
    }
    void open() {
        auto widget = std::make_unique<CommandPalette>(
            CommandPalette::Labels{L"Ara", L"Esc", L"Enter", L"Sonuçlar", L"Komutlar", L"Sonuç yok", L"İpucu"},
            [](const std::wstring& query) {
                PaletteResults found;
                auto item = [](PaletteItem::Kind kind, const wchar_t* name) {
                    PaletteItem i;
                    i.kind = kind;
                    i.name = name;
                    return i;
                };
                if (query == L"none") {
                    return found;
                }
                if (!query.empty()) {
                    found.results = {item(Kind::Component, L"Defender"), item(Kind::Service, L"WinDefend")};
                    found.total = 2;
                }
                found.commands = {item(Kind::Command, L"Kuyruğu uygula")};
                return found;
            });
        palette = widget.get();
        palette->onClose = [this] {
            events.push_back(L"close");
            host->popModal(palette);
            palette = nullptr;
        };
        palette->onRun = [this](const PaletteItem& item) { events.push_back(L"run:" + item.name); };
        host->pushModal(std::move(widget), &palette->input());
    }
    void type(std::wstring_view text) {
        for (const wchar_t c : text) {
            host->onChar(c);
        }
    }
    void key(UINT vk, bool ctrl = false) { host->onKeyDown({vk, ctrl, false, false}); }
    void click(ui::PointF p) {
        host->onPointer({ui::PointerAction::Move, p, ui::HitZone::Client});
        host->onPointer({ui::PointerAction::Down, p, ui::HitZone::Client});
        host->onPointer({ui::PointerAction::Up, p, ui::HitZone::Client});
    }
};

} // namespace

TEST_CASE("palette widget: typing searches, arrows wrap, Enter closes and then runs the selected item") {
    PaletteHarness h;
    REQUIRE(h.host->focused() == &h.palette->input());
    CHECK(h.palette->found().results.empty());
    CHECK(h.palette->found().commands.size() == 1);
    // 560 wide, centred, 120 from the top.
    CHECK(h.palette->panel().x == 220);
    CHECK(h.palette->panel().y == 120);
    CHECK(h.palette->panel().width == 560);

    h.type(L"def");
    CHECK(h.palette->found().results.size() == 2);
    CHECK(h.palette->selected() == 0);
    h.key(VK_DOWN);
    h.key(VK_DOWN);
    CHECK(h.palette->selected() == 2); // the command, after the two results
    h.key(VK_DOWN);
    CHECK(h.palette->selected() == 0); // wraps
    h.key(VK_UP);
    CHECK(h.palette->selected() == 2);
    h.key(VK_UP);
    CHECK(h.palette->selected() == 1);
    h.key(VK_RETURN);
    CHECK(h.events == std::vector<std::wstring>{L"close", L"run:WinDefend"});
    CHECK_FALSE(h.host->hasModal());
}

TEST_CASE("palette widget: → takes the greyed completion; Esc, Ctrl+K and a click outside only close") {
    PaletteHarness h;
    h.type(L"def");
    h.key(VK_RIGHT); // "Defender" begins with what was typed
    CHECK(h.palette->input().text() == L"defender"); // what was typed stays as typed
    h.key(VK_ESCAPE); // closes whatever the text is
    CHECK(h.events == std::vector<std::wstring>{L"close"});

    h.open();
    h.key('K', /*ctrl=*/true);
    CHECK(h.events.size() == 2);
    CHECK_FALSE(h.host->hasModal());

    h.open();
    h.type(L"none");
    CHECK(h.palette->found().results.empty());
    h.key(VK_RETURN); // nothing to run: stays open
    CHECK(h.host->hasModal());
    h.click({20, 20}); // the scrim
    CHECK(h.events.size() == 3);
    CHECK(h.events.back() == L"close");

    // A click on a row runs it.
    h.open();
    h.type(L"def");
    const ui::RectF panel = h.palette->panel();
    h.click({panel.x + 100, panel.y + 40 + 8 + 24 + 32 + 16}); // second result row
    CHECK(h.events.back() == L"run:WinDefend");
    CHECK_FALSE(h.host->hasModal());
}
