// 2026-11-10 UI polish of the pages: what a tree's category row says about what is under it, the
// column heads of a filter row over their cells, one empty state for every page, and the Diller read
// that a refresh started again and again (the render that spun at 100 % of a core).
#include "app/Format.h"
#include "app/Localization.h"
#include "app/catalog/AppxCatalog.h"
#include "app/controllers/ComponentController.h"
#include "app/controllers/FeatureController.h"
#include "app/controllers/ImageDriverController.h"
#include "app/controllers/LanguageController.h"
#include "app/pages/ComponentsPage.h"
#include "app/pages/DriversPage.h"
#include "app/pages/FeaturesPage.h"
#include "app/state/AppState.h"
#include "core/image/dism/OptionalFeatures.h"
#include "support/TestGraphics.h"
#include "support/UiThread.h"
#include "ui/widget/Host.h"
#include "ui/widgets/EmptyState.h"

#include <doctest.h>

#include <cmath>
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
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"pages-polish";
    std::filesystem::create_directories(dir);
    return dir / name;
}

const Localization& turkish() {
    static const Localization strings =
        Localization::fromJson(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json")).value();
    return strings;
}

const std::filesystem::path kMount = L"C:\\m";
constexpr std::uint64_t kMiB = 1024 * 1024;

// A root that places one child at `rect`.
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

// The page under a host (fonts for measuring), laid out at `rect`.
struct Hosted {
    std::unique_ptr<ui::Host> host;
    ui::Widget* page = nullptr;

    Hosted(std::unique_ptr<ui::Widget> body, RectF rect) {
        page = body.get();
        host = std::make_unique<ui::Host>(ui::HostServices{
            [] {},
            [](UINT, UINT) {},
            [](UINT) {},
            test::graphics().text.get(),
        });
        host->setRoot(std::make_unique<OneChild>(std::move(body), rect));
        host->layout({rect.right() + 20, rect.bottom() + 20});
    }
};

const ui::EmptyState* emptyStateOf(const ui::Widget& page) {
    for (const auto& child : page.children()) {
        if (const auto* empty = dynamic_cast<const ui::EmptyState*>(child.get())) {
            return empty;
        }
    }
    return nullptr;
}

core::AppxComponent appx(const wchar_t* identity, std::uint64_t size) {
    core::AppxComponent c;
    c.package.displayName = identity;
    c.package.packageName = std::wstring(identity) + L"_1.0.0.0_neutral_~_8wekyb3d8bbwe";
    c.size = size;
    return c;
}

core::OptionalFeature capability(std::wstring name, core::ServicingState state, std::uint64_t size) {
    core::OptionalFeature f;
    f.kind = core::OptionalFeature::Kind::Capability;
    f.name = name;
    f.displayName = std::move(name);
    f.state = state;
    f.size = size;
    return f;
}

} // namespace

TEST_CASE("components page: a category row says kept/all and its size, worked out when the queue changes") {
    AppState state{scratch(L"recent-c.json"), scratch(L"settings-c.json")};
    auto catalog = AppxCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/appx.json"));
    auto system = ComponentCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/components.json"));
    REQUIRE(catalog);
    REQUIRE(system);
    test::UiThread ui;
    ComponentController controller(state, std::move(*catalog), Language::Turkish, ui.post(), std::move(*system));
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, kMount,
                                         {appx(L"Microsoft.GamingApp", 400 * kMiB), appx(L"Microsoft.XboxGamingOverlay", 90 * kMiB),
                                          appx(L"Microsoft.WindowsStore", 50 * kMiB), appx(L"Contoso.Unknown", 3 * kMiB)},
                                         {}});
    const auto groups = controller.groups();
    REQUIRE(groups.size() == 3);
    REQUIRE(groups[0].items.size() == 2);

    auto body = std::make_unique<ComponentsPage>(state, controller, turkish(), Language::Turkish, [] {});
    ComponentsPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 1000, 600});
    // The categories, closed: each one's "kept/all" and its components' size in the size column's
    // format ("490.00": MB, as the components' own numbers). The catalog's own system components
    // (unknown to this fake image) add categories of their own: the rows are found by what they say.
    auto rowOf = [&](const std::wstring& count, const std::wstring& size) {
        for (int row = 0; row < page->rowCount(); ++row) {
            if (page->rowCountText(row) == count && page->rowSizeText(row) == size) {
                return row;
            }
        }
        return -1;
    };
    const int xbox = rowOf(L"2/2", L"490.00");
    REQUIRE(xbox >= 0);
    CHECK(rowOf(L"1/1", L"3.00") >= 0); // Kategorisiz: Contoso.Unknown

    // Unticking a component (queuing its removal) changes the row's count, not its size.
    controller.toggle(groups[0].items.front());
    CHECK(page->rowCountText(xbox) == L"1/2");
    CHECK(page->rowSizeText(xbox) == L"490.00");
    controller.toggle(groups[0].items.front());
    CHECK(page->rowCountText(xbox) == L"2/2");

    // "Boyut (MB)" starts where the size numbers do. D-139: it is the table's own head now (the header
    // row above it has the summary at its right), under the filter row.
    const RectF head = page->sizeHead();
    CHECK(std::abs(head.x - page->sizeNumberX()) < 0.01f);
    CHECK(head.y > page->bounds().y + ui::tokens::size::control);
    CHECK(head.right() <= page->bounds().right() + 0.01f);
}

TEST_CASE("features page: a category says on/all and the not-installed size; the heads sit over their cells") {
    using S = core::ServicingState;
    AppState state{scratch(L"recent-f.json"), scratch(L"settings-f.json")};
    test::UiThread ui;
    FeatureController controller{state, ui.post()};
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    state.setOptionalFeatures(AppState::OptionalFeatures{
        AppState::OptionalFeatures::Status::Ready,
        kMount,
        {capability(L"Language.Basic~~~tr-TR~0.0.1.0", S::Installed, 20 * kMiB),
         capability(L"Language.Handwriting~~~tr-TR~0.0.1.0", S::Installed, 12 * kMiB),
         capability(L"OpenSSH.Client~~~~0.0.1.0", S::Installed, 6 * kMiB),
         capability(L"Media.WindowsMediaPlayer~~~~0.0.12.0", S::NotPresent, 5 * kMiB)},
        {}});

    auto body = std::make_unique<FeaturesPage>(state, controller, turkish(), Language::Turkish, [] {});
    FeaturesPage* page = body.get();
    Hosted hosted(std::move(body), {0, 0, 1000, 600});
    // Diller and Diğer, closed. Only what is not installed counts for the size ("Yüklendi").
    REQUIRE(page->rowCount() == 2);
    CHECK(page->rowCountText(0) == L"2/2");
    CHECK(page->rowSizeText(0).empty());
    CHECK(page->rowCountText(1) == L"1/2");
    CHECK(page->rowSizeText(1) == formatBytes(5 * kMiB, Language::Turkish));

    // OpenSSH unticked: Diğer has none of its two on now.
    controller.toggle(state.optionalFeatures()->items[2]);
    CHECK(page->rowCountText(1) == L"0/2");

    // "Etkin" is centred over the box its check icons are centred in (it was 6 DIP to the left), and
    // "Yüklendi" ends where the sizes do.
    // (D-139: the marks column after the name moved them one to the right.)
    const RectF enabled = page->enabledHead();
    const RectF enabledCell = page->cellBox(3);
    CHECK(std::abs((enabled.x + enabled.width / 2) - (enabledCell.x + enabledCell.width / 2)) < 0.01f);
    CHECK(std::abs(page->sizeHead().right() - page->cellBox(4).right()) < 0.01f);
}

TEST_CASE("empty state: without a mounted image the page's empty state takes the whole body, as on every page") {
    AppState state{scratch(L"recent-e.json"), scratch(L"settings-e.json")};
    test::UiThread ui;
    ImageDriverController drivers(state, ImageDriverController::Events{ui.post(), {}, {}});
    FeatureController features{state, ui.post()};
    const RectF rect{0, 0, 900, 500};

    // The drivers page has tabs over its body: its empty state was laid out under them, 41 DIP
    // lower than every other page's.
    Hosted driversPage(std::make_unique<DriversPage>(state, drivers, turkish(), Language::Turkish, DriversPage::Intents{}), rect);
    Hosted featuresPage(std::make_unique<FeaturesPage>(state, features, turkish(), Language::Turkish, [] {}), rect);
    for (const Hosted* hosted : {&driversPage, &featuresPage}) {
        const ui::EmptyState* empty = emptyStateOf(*hosted->page);
        REQUIRE(empty);
        CHECK(empty->visible());
        CHECK(empty->bounds().x == rect.x);
        CHECK(empty->bounds().y == rect.y);
        CHECK(empty->bounds().width == rect.width);
        CHECK(empty->bounds().height == rect.height);
    }

    // A mounted image with its features read: the empty state goes.
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    // The mount starts the pages' reads on the engine thread; their results land on this thread
    // (test::UiThread) before the list below. They used to run on the engine thread, racing this one.
    ui.settle(state.engine(), state.reader());
    state.setOptionalFeatures(AppState::OptionalFeatures{
        AppState::OptionalFeatures::Status::Ready, kMount,
        {capability(L"OpenSSH.Client~~~~0.0.1.0", core::ServicingState::Installed, 6 * kMiB)}, {}});
    CHECK_FALSE(emptyStateOf(*featuresPage.page)->visible());
}

TEST_CASE("languages: a failed read is not started again by the page's refresh (the endless DISM loop)") {
    AppState state{scratch(L"recent-l.json"), scratch(L"settings-l.json")};
    test::UiThread ui;
    LanguageController languages(state, ui.post());
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    // The read failed (no DISM, a mount gone bad): the state says so.
    state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Failed, kMount, {}, {}, fail(ErrorCode::Unknown, L"x").error()});
    // The page's refresh (on that very change) asks for the image's settings again: nothing starts.
    // It used to read again at once, fail, refresh, read again … for as long as the page was open.
    int notified = 0;
    const auto subscription = state.subscribe([&](AppState::Change change) {
        if (change == AppState::Change::Intl) {
            ++notified;
        }
    });
    languages.load();
    languages.load();
    REQUIRE(state.imageIntl());
    CHECK(state.imageIntl()->status == AppState::ImageIntl::Status::Failed);
    CHECK(notified == 0);
    state.engine().drain();
    CHECK(ui.pending() == 0);
    state.unsubscribe(subscription);
}
