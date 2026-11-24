// D-139: the working pages' common pieces — the detail card's breakpoint, the columns a table leaves
// out beside it, the header row's summary line, and what each page's card says for a plain row, a
// queued row, a held / locked row and no selection; the unified summaries those pages compute.
#include "app/Format.h"
#include "app/Localization.h"
#include "app/catalog/AppxCatalog.h"
#include "app/controllers/ComponentController.h"
#include "app/controllers/FeatureController.h"
#include "app/controllers/FilesController.h"
#include "app/controllers/HostsController.h"
#include "app/controllers/PostSetupController.h"
#include "app/controllers/RegistryController.h"
#include "app/controllers/ServiceController.h"
#include "app/controllers/TaskController.h"
#include "app/pages/ComponentsPage.h"
#include "app/pages/DetailCard.h"
#include "app/pages/FeaturesPage.h"
#include "app/pages/FilesPage.h"
#include "app/pages/HostsPage.h"
#include "app/pages/PageBits.h"
#include "app/pages/PostSetupPage.h"
#include "app/pages/PageHeader.h"
#include "app/pages/RegistryPage.h"
#include "app/pages/ServicesPage.h"
#include "app/pages/TasksPage.h"
#include "app/state/AppState.h"
#include "core/image/RegistryEdit.h"
#include "core/image/RegistryInput.h"
#include "core/image/dism/OptionalFeatures.h"
#include "support/TestGraphics.h"
#include "support/UiThread.h"
#include "ui/widget/Host.h"
#include "ui/widgets/TableView.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::app;
using ui::RectF;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"detail-card";
    std::filesystem::create_directories(dir);
    return dir / name;
}

const Localization& turkish() {
    static const Localization strings =
        Localization::fromJson(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json")).value();
    return strings;
}

std::string catalog(const wchar_t* name) {
    return readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog" / name);
}

const std::filesystem::path kMount = L"C:\\m";
constexpr std::uint64_t kMiB = 1024 * 1024;

class OneChild : public ui::Widget {
public:
    OneChild(std::unique_ptr<ui::Widget> child, RectF rect) : m_rect(rect) { addChild(std::move(child)); }
    void layout() override {
        if (!children().empty()) {
            children()[0]->setBounds(m_rect);
        }
    }

private:
    RectF m_rect;
};

// The page (or any widget) under a host with fonts, laid out at `rect`.
struct Hosted {
    std::unique_ptr<ui::Host> host;
    ui::Widget* page = nullptr;

    Hosted(std::unique_ptr<ui::Widget> body, RectF rect) {
        page = body.get();
        host = std::make_unique<ui::Host>(ui::HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, test::graphics().text.get()});
        host->setRoot(std::make_unique<OneChild>(std::move(body), rect));
        host->layout({rect.right() + 20, rect.bottom() + 20});
    }
};

// The page's (first visible) table: rows are selected through it, as a click does.
ui::TableView* tableOf(ui::Widget& page) {
    for (const auto& child : page.children()) {
        if (auto* table = dynamic_cast<ui::TableView*>(child.get()); table && table->visible()) {
            return table;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("detail card: the breakpoint keeps the page card at least kDetailMinPageCard beside the card") {
    // The page card's width without the side card: the window less the navigation card and the gutters
    // (1207 DIP: 947; the minimum window 1100: 840).
    CHECK(detailCardFits(947.0f));
    CHECK_FALSE(detailCardFits(840.0f));
    // The edge: what is left after the card (size::inspector) and its gutter (size::contentPad).
    const float edge = kDetailMinPageCard + ui::tokens::size::inspector + ui::tokens::size::contentPad;
    CHECK(detailCardFits(edge));
    CHECK_FALSE(detailCardFits(edge - 1.0f));
}

TEST_CASE("detail card: beside the card a table leaves out the columns it repeats until the rest fits; without it all are back") {
    ui::TableView table(std::vector<ui::TableColumn>{
        {L"Ad", 0},
        {L"", marksColumnWidth(2)},
        {.title = L"Kimlik", .width = 206, .minWidth = 130},
        {.title = L"Varsayılan", .width = 172, .minWidth = 140},
        {.title = L"Yeni", .width = 212, .minWidth = 184},
    });
    table.setFlexMin(kNameColumnMin);
    table.setBounds({0, 0, 602, 400});
    // All shown: the flexible column at its least plus every fixed one at its own least.
    CHECK(table.minimumWidth() == doctest::Approx(kNameColumnMin + marksColumnWidth(2) + 130 + 140 + 184));
    fitColumns(table, 602, /*detailShown=*/true, {2, 3});
    CHECK(table.columnHidden(2));      // the key name went first
    CHECK_FALSE(table.columnHidden(3)); // the rest fits now (the name column may narrow to kDetailFlexMin)
    CHECK(table.minimumWidth() <= 602.0f);
    // The hidden column takes no room: its neighbours share the table's width.
    const auto edges = table.columnEdges();
    CHECK(edges[2] == doctest::Approx(edges[3]));
    CHECK(edges.back() == doctest::Approx(602.0f));
    // Narrower still: the next one goes too.
    fitColumns(table, 450, true, {2, 3});
    CHECK(table.columnHidden(2));
    CHECK(table.columnHidden(3));
    // No card: every column again, whatever the width.
    fitColumns(table, 450, false, {2, 3});
    CHECK_FALSE(table.columnHidden(2));
    CHECK_FALSE(table.columnHidden(3));
}

TEST_CASE("summary line: quiet facts go first when the row is short, then the short forms, then the text is cut") {
    auto line = std::make_unique<SummaryLine>();
    SummaryLine* summary = line.get();
    Hosted hosted(std::move(line), {0, 0, 600, ui::tokens::size::control});
    summary->set({{L"214 servis"}, {L"2 devre dışı bırakılacak", SummaryPart::Tone::Change, L"2 değişecek"}});
    CHECK(summary->text() == L"214 servis \u00b7 2 devre dışı bırakılacak");
    CHECK(summary->shownText() == summary->text());
    // The same parts again: nothing changes (no layout, no repaint for an equal line).
    summary->set({{L"214 servis"}, {L"2 devre dışı bırakılacak", SummaryPart::Tone::Change, L"2 değişecek"}});
    CHECK(summary->parts().size() == 2);

    Hosted narrow(std::make_unique<SummaryLine>(), {0, 0, 150, ui::tokens::size::control});
    auto* shortRow = static_cast<SummaryLine*>(narrow.page);
    shortRow->set({{L"214 servis"}, {L"2 devre dışı bırakılacak", SummaryPart::Tone::Change, L"2 değişecek"}});
    CHECK(shortRow->shownText() == L"2 devre dışı bırakılacak"); // the quiet count made room
    Hosted tiny(std::make_unique<SummaryLine>(), {0, 0, 90, ui::tokens::size::control});
    auto* least = static_cast<SummaryLine*>(tiny.page);
    least->set({{L"214 servis"}, {L"2 devre dışı bırakılacak", SummaryPart::Tone::Change, L"2 değişecek"}});
    CHECK(least->shownText() == L"2 değişecek"); // then its short form
    // Empty parts are no parts.
    least->set({{L""}, {L"", SummaryPart::Tone::Change}});
    CHECK(least->parts().empty());
    CHECK(least->text().empty());
}

TEST_CASE("components: the card for no selection, a kept item, a queued item, a held item; the header's summary") {
    AppState state{scratch(L"recent-c.json"), scratch(L"settings-c.json")};
    auto apps = AppxCatalog::parse(catalog(L"appx.json"));
    auto system = ComponentCatalog::parse(catalog(L"components.json"));
    REQUIRE(apps.has_value());
    REQUIRE(system.has_value());
    test::UiThread ui;
    ComponentController controller(state, std::move(*apps), Language::Turkish, ui.post(), std::move(*system));
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    ui.settle(state.engine());
    auto appx = [](const wchar_t* identity, std::uint64_t size) {
        core::AppxComponent c;
        c.package.displayName = identity;
        c.package.packageName = std::wstring(identity) + L"_1.0.0.0_neutral_~_8wekyb3d8bbwe";
        c.size = size;
        return c;
    };
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, kMount,
                                         {appx(L"Microsoft.GamingApp", 400 * kMiB), appx(L"Microsoft.BingWeather", 20 * kMiB)}, {}});
    auto body = std::make_unique<ComponentsPage>(state, controller, turkish(), Language::Turkish, [] {});
    ComponentsPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});
    int notices = 0;
    page->onDetailChanged = [&] { ++notices; };

    // Nothing selected: the card's "Bir satır seç".
    CHECK(page->detailAvailable());
    CHECK(page->detail().empty());

    std::wstring gaming;
    std::wstring weather;
    for (const auto& g : controller.groups()) {
        for (const auto& item : g.items) {
            if (item.identity == L"Microsoft.GamingApp") {
                gaming = item.packageName;
            } else if (item.identity == L"Microsoft.BingWeather") {
                weather = item.packageName;
            }
        }
    }
    REQUIRE_FALSE(gaming.empty());
    page->reveal(gaming);
    CHECK(notices > 0); // the selection told the shell
    DetailModel model = page->detail();
    REQUIRE_FALSE(model.empty());
    CHECK(model.identity == L"Microsoft.GamingApp");
    CHECK(model.state.text == turkish().get(Str::ComponentsStays));
    CHECK(model.state.tone == DetailTone::Quiet);
    REQUIRE(model.fact(turkish().get(Str::CommonSize)));
    CHECK(model.fact(turkish().get(Str::CommonSize))->value == formatBytes(400 * kMiB, Language::Turkish));
    REQUIRE(model.action(ComponentsPage::kActionToggle));
    CHECK(model.action(ComponentsPage::kActionToggle)->label == turkish().get(Str::ComponentsAddToQueue));
    CHECK(model.action(ComponentsPage::kActionToggle)->enabled);

    // The card's button does what Space does: queued now, and the card says so in the accent.
    page->detailAction(ComponentsPage::kActionToggle);
    model = page->detail();
    CHECK(model.state.text == turkish().get(Str::ComponentsWillRemove));
    CHECK(model.state.tone == DetailTone::Change);
    CHECK(model.fact(turkish().get(Str::DetailAtApply))->tone == DetailTone::Change);
    CHECK(model.action(ComponentsPage::kActionToggle)->label == turkish().get(Str::ComponentsRemoveFromQueue));
    // The header row: what the image has, then what Uygula removes.
    CHECK(page->summaryText().find(turkish().format(Str::ComponentsSummaryRemove,
                                                    {{L"n", L"1"}, {L"size", formatBytes(400 * kMiB, Language::Turkish)}})) !=
          std::wstring::npos);

    // A guard keeps the other one: held, its button off, the guard's words in a note.
    controller.blockOf = [&](const core::ops::Operation& op) {
        core::ops::CompatBlock block;
        if (op.target == weather) {
            block.guards.push_back(L"recommended");
        }
        return block;
    };
    page->explainBlock = [](const ComponentController::Item&) { return std::wstring(L"Korunuyor: Önerilen"); };
    page->refresh();
    page->reveal(weather);
    model = page->detail();
    CHECK(model.state.text == turkish().get(Str::MarksHeld));
    CHECK_FALSE(model.action(ComponentsPage::kActionToggle)->enabled);
    REQUIRE_FALSE(model.notes.empty());
    CHECK(model.notes.front().text.starts_with(L"Korunuyor: Önerilen"));

    // The list goes (unmounted): no card beside the page's empty state.
    state.setMounted(std::nullopt);
    ui.settle(state.engine());
    CHECK_FALSE(page->detailAvailable());
    CHECK(page->detail().empty());
}

TEST_CASE("services: the card for a plain, a queued and a guarded service; the summary counts what is disabled") {
    using core::ServiceEntry;
    using core::StartType;
    AppState state{scratch(L"recent-s.json"), scratch(L"settings-s.json")};
    const std::string servicesCatalog = catalog(L"services.json");
    test::UiThread ui;
    ServiceController controller{state, servicesCatalog, ui.post()};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    ui.settle(state.engine());
    auto service = [](std::wstring name, StartType start, std::vector<std::wstring> deps = {}) {
        ServiceEntry s;
        s.name = name;
        s.displayName = name + L" hizmeti";
        s.description = L"Bir açıklama.";
        s.account = L"LocalSystem";
        s.imagePath = L"%SystemRoot%\\system32\\svchost.exe -k netsvcs";
        s.start = start;
        s.type = 0x20;
        s.dependsOn = std::move(deps);
        return s;
    };
    state.setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Ready, kMount,
                                               {service(L"DiagTrack", StartType::Auto, {L"RpcSs"}), service(L"RpcSs", StartType::Auto),
                                                service(L"wuauserv", StartType::Manual)},
                                               {}});
    auto body = std::make_unique<ServicesPage>(state, controller, turkish(), Language::Turkish, ServicesPage::Intents{});
    ServicesPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});

    CHECK(page->detail().empty());
    CHECK(page->summaryText() == turkish().format(Str::ServicesSummary, {{L"n", L"3"}}));

    page->reveal(L"DiagTrack");
    DetailModel model = page->detail();
    REQUIRE_FALSE(model.empty());
    CHECK(model.title == L"DiagTrack hizmeti");
    CHECK(model.identity == L"DiagTrack");
    CHECK(model.state.text == turkish().get(Str::ServicesKeeps));
    CHECK(model.about.starts_with(L"Bir açıklama."));
    CHECK(model.fact(turkish().get(Str::ServicesDefault))->value == turkish().get(Str::ServicesStartAuto));
    CHECK(model.fact(turkish().get(Str::ServicesAccount))->value == L"LocalSystem");
    // What it needs (DependOnService).
    REQUIRE_FALSE(model.lists.empty());
    CHECK(model.lists.front().lines == std::vector<std::wstring>{L"RpcSs"});
    CHECK(model.action(ServicesPage::kActionChoose));
    CHECK_FALSE(model.action(ServicesPage::kActionRevert));

    // Queued: disabled at Apply — the state and the fact in the accent, "Varsayılana dön" offered.
    REQUIRE(controller.set(state.serviceList()->items[0], StartType::Disabled));
    model = page->detail();
    CHECK(model.state.tone == DetailTone::Change);
    CHECK(model.state.text == turkish().format(Str::ServicesWillBecome, {{L"start", turkish().get(Str::ServicesStartDisabled)}}));
    CHECK(model.fact(turkish().get(Str::DetailAtApply))->tone == DetailTone::Change);
    REQUIRE(model.action(ServicesPage::kActionRevert));
    CHECK(page->summaryText() == turkish().format(Str::ServicesSummary, {{L"n", L"3"}}) + L" \u00b7 " +
                                     turkish().format(Str::ServicesSummaryDisable, {{L"n", L"1"}}));
    // The card's "Varsayılana dön" is the menu's own start type: out of the queue again.
    page->detailAction(ServicesPage::kActionRevert);
    CHECK_FALSE(controller.changed(state.serviceList()->items[0]));

    // RpcSs is needed by DiagTrack: the card lists who needs it.
    page->reveal(L"RpcSs");
    model = page->detail();
    bool neededBy = false;
    for (const auto& list : model.lists) {
        neededBy = neededBy || (list.title == turkish().get(Str::DetailNeededBy) && list.lines == std::vector<std::wstring>{L"DiagTrack"});
    }
    CHECK(neededBy);

    // A guard keeps wuauserv from being disabled: held, with the guard's words.
    controller.blockOf = [](const core::ops::Operation& op) {
        core::ops::CompatBlock block;
        if (op.target == L"wuauserv") {
            block.guards.push_back(L"windows-update");
        }
        return block;
    };
    page->explainGuard = [](const ServiceEntry&) { return std::wstring(L"Korunuyor: Windows Update"); };
    page->reveal(L"wuauserv");
    model = page->detail();
    CHECK(model.state.text == turkish().get(Str::MarksHeld));
    REQUIRE_FALSE(model.notes.empty());
    CHECK(model.notes.front().text.starts_with(L"Korunuyor: Windows Update"));
}

TEST_CASE("registry: the card for a typed value on and off, an imported file, and no selection; the summary") {
    AppState state{scratch(L"recent-r.json"), scratch(L"settings-r.json")};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    state.engine().drain();
    RegistryController controller{state};
    auto body = std::make_unique<RegistryPage>(state, controller, turkish(), Language::Turkish, RegistryPage::Intents{});
    RegistryPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});
    CHECK(page->detail().empty());
    REQUIRE(controller.addValue(*core::registryWriteFromInput(LR"(HKLM\SOFTWARE\Contoso)", L"Mode", core::RegValueType::Dword, L"0x10"),
                                /*afterSetup=*/false));
    controller.addImport(L"C:\\t.reg", *core::parseRegText(L"REGEDIT4\n[HKEY_CURRENT_USER\\Software\\B]\n\"a\"=dword:00000001\n\"b\"=\"x\"\n"));
    CHECK(page->summaryText() == turkish().format(Str::RegistrySummary, {{L"n", L"2"}}) + L" \u00b7 " +
                                     turkish().format(Str::RegistrySummaryOn, {{L"n", L"2"}}));

    ui::TableView* table = tableOf(*page);
    REQUIRE(table);
    table->setSelected(0);
    DetailModel model = page->detail();
    REQUIRE_FALSE(model.empty());
    CHECK(model.title == L"Mode");
    CHECK(model.identity == LR"(HKLM\SOFTWARE\Contoso)");
    CHECK(model.state.text == turkish().get(Str::RegistryWillWrite));
    CHECK(model.fact(turkish().get(Str::RegistryValueData)));
    CHECK(model.fact(turkish().get(Str::RegistryAfterSetupFact))->value == turkish().get(Str::RegistryAfterSetupNo));
    CHECK(model.action(RegistryPage::kActionEdit));
    CHECK(model.action(RegistryPage::kActionToggle)->label == turkish().get(Str::RegistryTurnOff));

    // Off: the card says it is not applied; the summary counts one.
    page->detailAction(RegistryPage::kActionToggle);
    model = page->detail();
    CHECK(model.state.text == turkish().get(Str::RegistryOff));
    CHECK(model.state.tone == DetailTone::Quiet);
    CHECK(model.action(RegistryPage::kActionToggle)->label == turkish().get(Str::RegistryTurnOn));
    CHECK(page->summaryText().ends_with(turkish().format(Str::RegistrySummaryOn, {{L"n", L"1"}})));

    // The imported file: its values listed, no "Düzenle…".
    table->setSelected(1);
    model = page->detail();
    CHECK(model.title == L"t.reg");
    CHECK_FALSE(model.action(RegistryPage::kActionEdit));
    REQUIRE(model.lists.size() == 1);
    CHECK(model.lists.front().lines.size() == 2);

    // "Kaldır": the row's Delete.
    page->detailAction(RegistryPage::kActionRemove);
    CHECK(state.regImports().size() == 1);
}

TEST_CASE("features: the card for a capability kept, queued for removal, held by a guard; no selection") {
    using S = core::ServicingState;
    AppState state{scratch(L"recent-f.json"), scratch(L"settings-f.json")};
    test::UiThread ui;
    FeatureController controller{state, ui.post()};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    ui.settle(state.engine());
    auto capability = [](std::wstring name, std::uint64_t size) {
        core::OptionalFeature f;
        f.kind = core::OptionalFeature::Kind::Capability;
        f.name = name;
        f.displayName = name.substr(0, name.find(L'~'));
        f.description = L"Açıklaması.";
        f.state = S::Installed;
        f.size = size;
        return f;
    };
    state.setOptionalFeatures(AppState::OptionalFeatures{
        AppState::OptionalFeatures::Status::Ready, kMount,
        {capability(L"OpenSSH.Client~~~~0.0.1.0", 6 * kMiB), capability(L"Media.WindowsMediaPlayer~~~~0.0.12.0", 5 * kMiB)}, {}});
    auto body = std::make_unique<FeaturesPage>(state, controller, turkish(), Language::Turkish, [] {});
    FeaturesPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});
    CHECK(page->detail().empty());
    CHECK(page->summaryText() == turkish().format(Str::FeaturesSummary, {{L"on", L"2"}, {L"n", L"2"}}));

    page->reveal(L"OpenSSH.Client~~~~0.0.1.0");
    DetailModel model = page->detail();
    REQUIRE_FALSE(model.empty());
    CHECK(model.identity == L"OpenSSH.Client~~~~0.0.1.0");
    CHECK(model.about == L"Açıklaması.");
    CHECK(model.state.text == turkish().get(Str::DetailUnchanged));
    CHECK(model.fact(turkish().get(Str::DetailInImage))->value == turkish().get(Str::FeaturesInstalled));
    CHECK(model.action(FeaturesPage::kActionToggle)->label == turkish().get(Str::FeaturesRemoveCap));

    page->detailAction(FeaturesPage::kActionToggle);
    model = page->detail();
    CHECK(model.state.tone == DetailTone::Change);
    CHECK(model.state.text == turkish().get(FeaturesPage::statusName(FeatureController::Status::WillRemove)));
    CHECK(model.action(FeaturesPage::kActionToggle)->label == turkish().get(Str::FeaturesUndoChange));
    CHECK(page->summaryText().ends_with(turkish().format(Str::FeaturesSummaryChange, {{L"n", L"1"}})));

    controller.blockOf = [](const core::ops::Operation& op) {
        core::ops::CompatBlock block;
        if (op.target.starts_with(L"Media.")) {
            block.guards.push_back(L"media");
        }
        return block;
    };
    page->explainGuard = [](const core::OptionalFeature&) { return std::wstring(L"Korunuyor: Medya"); };
    page->reveal(L"Media.WindowsMediaPlayer~~~~0.0.12.0");
    model = page->detail();
    CHECK(model.state.text == turkish().get(Str::MarksHeld));
    CHECK_FALSE(model.action(FeaturesPage::kActionToggle)->enabled);
    REQUIRE_FALSE(model.notes.empty());
    CHECK(model.notes.front().text.starts_with(L"Korunuyor: Medya"));
}

TEST_CASE("tasks: the card for a task left on and one switched off; the summary counts what Uygula switches off") {
    AppState state{scratch(L"recent-t.json"), scratch(L"settings-t.json")};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    state.engine().drain();
    const std::string tasksCatalog = catalog(L"tasks.json");
    TaskController controller{state, tasksCatalog};
    auto body = std::make_unique<TasksPage>(state, controller, turkish(), Language::Turkish, [] {});
    TasksPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});
    CHECK(page->detail().empty());
    ui::TableView* table = tableOf(*page);
    REQUIRE(table);
    REQUIRE(table->rowCount() > 0);
    table->setSelected(0);
    DetailModel model = page->detail();
    REQUIRE_FALSE(model.empty());
    CHECK(model.identity.starts_with(L"\\Microsoft\\Windows\\"));
    CHECK(model.state.tone == DetailTone::Quiet);
    CHECK(model.fact(turkish().get(Str::MarksRecommended)));
    const std::wstring before = page->summaryText();

    page->detailAction(TasksPage::kActionToggle); // the row's Space
    model = page->detail();
    CHECK(model.state.tone == DetailTone::Change);
    CHECK(model.state.text == turkish().get(Str::TasksStateWillDisable));
    CHECK(model.action(TasksPage::kActionToggle)->label == turkish().get(Str::TasksActionOn));
    CHECK(page->summaryText() != before);
    CHECK(page->summaryText().ends_with(turkish().format(Str::TasksSummaryOff, {{L"n", L"1"}})));
}

TEST_CASE("2c hosts: the card names what a list blocks; its button is the row's tick; the custom row imports") {
    AppState state{scratch(L"recent-h.json"), scratch(L"settings-h.json")};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    state.engine().drain();
    HostsController controller{state, catalog(L"hosts.json")};
    REQUIRE_FALSE(controller.lists().empty());
    int imports = 0;
    auto body = std::make_unique<HostsPage>(state, controller, turkish(), Language::Turkish, [] {}, [&] { ++imports; });
    HostsPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});
    CHECK(page->detailAvailable());
    CHECK(page->detail().empty()); // nothing selected: "Bir satır seç"
    page->selectRowForTests(0);
    DetailModel model = page->detail();
    REQUIRE_FALSE(model.empty());
    const HostsList& list = controller.lists().front();
    CHECK(model.title == list.name.get(Language::Turkish));
    CHECK(model.about == list.description.get(Language::Turkish));
    CHECK(model.state.text == turkish().get(Str::HostsStateNotSelected));
    REQUIRE(model.lists.size() == 1);
    CHECK(model.lists.front().lines.size() == list.entries.size()); // the names the table only counts
    CHECK(model.lists.front().lines.front() == list.entries.front().name);
    REQUIRE(model.fact(turkish().get(Str::CommonRisk)));
    CHECK(model.fact(turkish().get(Str::CommonRisk))->dot);
    CHECK(model.action(HostsPage::kActionToggle)->label == turkish().get(Str::HostsActionBlock));

    page->detailAction(HostsPage::kActionToggle); // as the row's tick
    CHECK(controller.on(list));
    model = page->detail();
    CHECK(model.state.tone == DetailTone::Change);
    CHECK(model.state.text == turkish().get(Str::HostsStateWillWrite));
    CHECK(model.action(HostsPage::kActionToggle)->label == turkish().get(Str::HostsActionUnblock));

    page->selectRowForTests(static_cast<int>(controller.lists().size())); // the custom row, empty
    model = page->detail();
    CHECK(model.title == turkish().get(Str::HostsCustom));
    CHECK(model.lists.empty());
    CHECK(model.action(HostsPage::kActionToggle)->label == turkish().get(Str::HostsImport));
    page->detailAction(HostsPage::kActionToggle);
    CHECK(imports == 1); // the same import the empty row's tick opens
    state.setMounted(std::nullopt);
    state.engine().drain();
    CHECK_FALSE(page->detailAvailable());
}

TEST_CASE("2c files: the risk is a dot with its reason; the card has the whole path; its button is Delete") {
    AppState state{scratch(L"recent-f.json"), scratch(L"settings-f.json")};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    state.engine().drain();
    const auto source = scratch(L"readme-for-the-image.txt");
    std::ofstream(source, std::ios::binary) << "hello";
    FilesController controller{state};
    REQUIRE(controller.add({source}, L"Tools").first == 1);
    REQUIRE(controller.add({source}, L"Windows\\Web").first == 1); // under Windows: risky
    auto body = std::make_unique<FilesPage>(state, controller, turkish(), Language::Turkish, [] {}, [] {});
    FilesPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});
    ui::TableView* table = tableOf(*page);
    REQUIRE(table);
    REQUIRE(table->rowCount() == 2);
    int risky = -1;
    for (int row = 0; row < 2; ++row) {
        page->selectRowForTests(row);
        const DetailModel model = page->detail();
        REQUIRE_FALSE(model.empty());
        CHECK(model.title == L"readme-for-the-image.txt");
        CHECK(model.about.starts_with(source.wstring() + L"\n")); // the whole source the table cuts
        CHECK(model.state.text == turkish().get(Str::FilesStateQueued));
        CHECK(model.subtitle.starts_with(turkish().get(Str::FilesKindFile)));
        if (!model.notes.empty()) {
            risky = row;
            CHECK(model.notes.front().text == turkish().get(Str::FilesRiskNote));
            CHECK(model.fact(turkish().get(Str::CommonRisk))->value == turkish().get(Str::RiskHigh));
            CHECK(model.about == turkish().format(Str::FilesCardAbout, {{L"source", source.wstring()},
                                                                       {L"path", L"C:\\Windows\\Web\\readme-for-the-image.txt"}}));
            CHECK(table->cellTooltip(row, 1).find(turkish().get(Str::FilesRiskNote)) != std::wstring::npos);
        } else {
            CHECK(table->cellTooltip(row, 1).empty()); // a safe row has no mark to explain
        }
    }
    REQUIRE(risky >= 0);
    page->selectRowForTests(risky);
    page->detailAction(FilesPage::kActionRemove); // the row's Delete
    CHECK(table->rowCount() == 1);
    CHECK(controller.count() == 1);
}

TEST_CASE("2c post-setup: the card says the whole command, when it runs, waiting; its buttons are the row's") {
    AppState state{scratch(L"recent-p.json"), scratch(L"settings-p.json")};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    state.engine().drain();
    PostSetupController controller{state};
    core::PostSetupStep command;
    command.type = core::PostSetupStep::Type::Command;
    command.name = L"Güç planı";
    command.source = L"powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c && powercfg /change standby-timeout-ac 0";
    command.wait = true;
    controller.add(command);
    core::PostSetupStep app;
    app.type = core::PostSetupStep::Type::Winget;
    app.name = L"7-Zip";
    app.source = L"7zip.7zip";
    controller.add(app);
    std::vector<std::size_t> edited;
    auto body = std::make_unique<PostSetupPage>(state, controller, turkish(), Language::Turkish,
                                                PostSetupPage::Intents{[&](std::size_t i) { edited.push_back(i); }, [] {}});
    PostSetupPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 900, 600});
    CHECK(page->detail().empty());
    page->selectRowForTests(0);
    DetailModel model = page->detail();
    REQUIRE_FALSE(model.empty());
    CHECK(model.title == L"Güç planı");
    CHECK(model.about == command.source); // the whole command line
    CHECK(model.state.text == turkish().format(Str::PostsetupStateFirstLogon, {{L"n", L"1"}}));
    CHECK(model.fact(turkish().get(Str::PostsetupCardOrder))->value == L"1 / 2");
    CHECK(model.fact(turkish().get(Str::PostsetupWait))->value == turkish().get(Str::CommonYes));
    REQUIRE(model.action(PostSetupPage::kActionWait));
    CHECK(model.action(PostSetupPage::kActionWait)->label == turkish().get(Str::PostsetupActionNoWait));
    page->detailAction(PostSetupPage::kActionWait); // the "Bekle" tag's click
    CHECK_FALSE(controller.plan().steps[0].wait);
    page->detailAction(PostSetupPage::kActionEdit); // Enter
    CHECK(edited == std::vector<std::size_t>{0});

    page->selectRowForTests(1);
    model = page->detail();
    CHECK(model.identity == L"7zip.7zip");
    CHECK_FALSE(model.action(PostSetupPage::kActionWait)); // only a command waits or not
    CHECK(model.fact(turkish().get(Str::PostsetupWait))->value == turkish().get(Str::PostsetupCardWaitAlways));
    page->detailAction(PostSetupPage::kActionDelete); // Delete
    CHECK(controller.plan().steps.size() == 1);
}

TEST_CASE("marks: one vocabulary — the icon and ink of each state, the tooltip's lines") {
    CHECK(markIcon(RowMark::Queued) == ui::icons::Icon::QueueClock);
    CHECK(markIcon(RowMark::Held) == ui::icons::Icon::ShieldCheck);
    CHECK(markIcon(RowMark::Locked) == ui::icons::Icon::Lock);
    CHECK(markInk(RowMark::Queued) == ui::tokens::Color::AccentText);
    CHECK(markInk(RowMark::Held) == ui::tokens::Color::TextTertiary);
    const auto& s = turkish();
    CHECK(marksTooltip(s, {}, L"x", L"y").empty());
    CHECK(marksTooltip(s, {RowMark::Queued}, L"Uygula'da kaldırılacak", {}) == L"Uygula'da kaldırılacak");
    CHECK(marksTooltip(s, {RowMark::Held}, {}, L"Korunuyor: Windows Update") ==
          s.get(Str::MarksHeld) + L": Korunuyor: Windows Update");
    const std::wstring risky = marksTooltip(s, {RowMark::None, core::ops::Risk::High, true}, {}, {}, L"not");
    CHECK(risky == s.get(Str::RiskColumn) + L": " + s.get(Str::RiskHigh) + L" \u2014 not\n" + s.get(Str::MarksRecommended));
    CHECK(marksColumnWidth(1) == doctest::Approx(2 * ui::TableView::kCellPad + kMarkSlot));
    CHECK(marksColumnWidth(3) == doctest::Approx(2 * ui::TableView::kCellPad + 3 * kMarkSlot + 2 * kMarkGap));
}
