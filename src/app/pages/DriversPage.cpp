#include "app/pages/DriversPage.h"
#include "core/image/BootImage.h"
#include "core/system/DeviceClasses.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "base/Text.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTabsTop = 0.0f;
constexpr float kTabsHeight = ui::tokens::size::control;
constexpr float kToolbarTop = kTabsTop + kTabsHeight + 11.0f;
constexpr float kToolbar = ui::tokens::size::control;
constexpr float kGap = 10.0f;
constexpr float kIndent = 16.0f;
constexpr float kChevron = 16.0f;
constexpr float kTagRoom = 220.0f; // D-134: the boot tab's word at the right of a name cell
constexpr const wchar_t* kMediaGroupKey = L"\x01media"; // D-134: the collapsed-set key of the boot.wim group
enum Column : int { kName, kProvider, kSize };
constexpr int kImageMarkSlots = 2; // D-139: queued, the risk (boot-critical)
enum ImageColumn : int { kDriver, kImageMarks, kClass, kImageProvider, kVersion, kDate };

ui::icons::Icon classIcon(const std::wstring& cls) {
    const std::wstring c = wl::text::lower(cls);
    if (c == L"net" || c == L"netservice" || c == L"bluetooth") {
        return ui::icons::Icon::Network;
    }
    if (c == L"scsiadapter" || c == L"hdc" || c == L"diskdrive") {
        return ui::icons::Icon::DiskPartition;
    }
    if (c == L"display" || c == L"monitor") {
        return ui::icons::Icon::ImageWim;
    }
    if (c == L"usb") {
        return ui::icons::Icon::UsbDrive;
    }
    return ui::icons::Icon::DriverChip;
}
} // namespace

std::wstring DriversPage::className(const std::wstring& cls, const Localization& strings) {
    static const std::map<std::wstring, Str> kNames{
        {L"net", Str::DriversClsNet},
        {L"scsiadapter", Str::DriversClsScsiadapter},
        {L"hdc", Str::DriversClsHdc},
        {L"display", Str::DriversClsDisplay},
        {L"system", Str::DriversClsSystem},
        {L"media", Str::DriversClsMedia},
        {L"usb", Str::DriversClsUsb},
        {L"bluetooth", Str::DriversClsBluetooth},
        {L"hidclass", Str::DriversClsHidclass},
        {L"camera", Str::DriversClsCamera},
        {L"printer", Str::DriversClsPrinter},
        {L"extension", Str::DriversClsExtension},
        {L"softwarecomponent", Str::DriversClsSoftwarecomponent},
        {L"firmware", Str::DriversClsFirmware},
    };
    if (cls.empty()) {
        return strings.get(Str::DriversClsNone);
    }
    // D-114: Windows' own name for the class, in this PC's language (Device Manager's, the reference tool's).
    if (const auto& info = core::deviceClassInfo({}, cls); !info.description.empty()) {
        return info.description;
    }
    const auto it = kNames.find(wl::text::lower(cls));
    if (it == kNames.end()) {
        return cls;
    }
    return strings.get(it->second);
}

DriversPage::DriversPage(AppState& state, ImageDriverController& images, const Localization& strings, Language language,
                         Intents intents)
    : m_state(state), m_images(images), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_tabs = &add<ui::TabBar>(std::vector<std::wstring>{strings.get(Str::DriversTabAdd), strings.get(Str::DriversTabImage),
                                                        strings.get(Str::DriversTabBoot)},
                              0);
    m_tabs->onChange = [this](int) {
        if (imageTab()) {
            m_images.load();
        }
        refresh();
        layout(); // the boot tab keeps a detail line under the tree
        detailChanged();
    };
    m_summaryLine = &add<SummaryLine>();
    m_imageTable = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::DriversDriver), 0},
        {L"", marksColumnWidth(kImageMarkSlots)},
        {.title = strings.get(Str::DriversClass), .width = 218, .minWidth = 150},
        {.title = strings.get(Str::DriversProviderName), .width = 230, .minWidth = 140},
        {.title = strings.get(Str::DriversVersion), .width = 150, .minWidth = 110},
        {.title = strings.get(Str::DriversDate), .width = 110, .minWidth = 96},
    });
    // D-136: at a narrow window the other columns give way to the driver's name.
    m_imageTable->setFlexMin(kNameColumnMin);
    m_imageTable->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintImageCell(c, row, column, rect, cell);
    };
    m_imageTable->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kDriver && p.x < m_imageTable->cellRect(row, kDriver).x + ui::TableView::kCellPad + ui::Checkbox::kBox + 6) {
            toggleImageRow(row);
        }
    };
    m_imageTable->onActivate = [this](int row) { toggleImageRow(row); };
    m_imageTable->onSelect = [this](int) {
        invalidate();
        detailChanged();
    };
    m_imageTable->cellTooltip = [this](int row, int column) -> std::wstring {
        if (column != kImageMarks) {
            return {};
        }
        return marksTooltip(m_strings, imageMarks(row), m_strings.get(Str::ComponentsWillRemove), {}, m_strings.get(Str::DriversBootCritical));
    };
    // D-136: a driver's name cut at a narrow window shows whole on hover (after the check and the class icon).
    m_imageTable->cellText = [this](int row, int column) -> ui::TableCellText {
        const auto& list = m_state.imageDrivers();
        if (column != kDriver || !list || row < 0 || row >= static_cast<int>(list->items.size())) {
            return {};
        }
        const auto& d = list->items[static_cast<std::size_t>(row)];
        return {d.originalFileName.empty() ? d.publishedName : d.originalFileName, TypeStyle::BodyStrong,
                ui::Checkbox::kBox + 8 + ui::tokens::size::icon + 6};
    };
    m_search = &add<ui::SearchBox>(strings.get(Str::DriversSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
        rebuild();
    };
    m_class = &add<ui::Dropdown>(strings.get(Str::DriversClass), std::vector<std::wstring>{strings.get(Str::CommonAll)}, 0);
    m_class->onChange = [this](int index) {
        m_classFilter = index;
        rebuild();
    };
    m_arch = &add<ui::Dropdown>(strings.get(Str::ImagesArch), archFilterItems(strings), 0);
    m_arch->onChange = [this](int index) {
        m_archFilter = index;
        rebuild();
    };
    // Default: the mounted edition's architecture.
    if (const auto& mounted = m_state.mounted(); mounted && m_state.source()) {
        for (const auto& image : m_state.source()->install.images) {
            if (image.index == mounted->index) {
                const std::wstring arch = core::architectureName(image.architecture);
                for (int i = 1; i < 4; ++i) {
                    if (arch == kArchFilterKeys[i]) {
                        m_archFilter = i;
                        m_arch->setSelected(i);
                    }
                }
            }
        }
    }
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::CommonName), 0},
        {.title = strings.get(Str::DriversProvider), .width = 300, .minWidth = 220},
        {strings.get(Str::CommonSize), 110, ui::TextAlign::Trailing},
    });
    m_table->setFlexMin(kNameColumnMin);
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column != kName || row < 0 || row >= static_cast<int>(m_rows.size())) {
            return;
        }
        const Row& r = m_rows[static_cast<std::size_t>(row)];
        const auto& group = m_groups[static_cast<std::size_t>(r.group)];
        const bool isGroup = r.inf < 0 && r.media < 0;
        const RectF cell = m_table->cellRect(row, column);
        const float x0 = cell.x + ui::TableView::kCellPad + (isGroup ? 0.0f : kIndent);
        if (isGroup && p.x < x0 + kChevron) {
            if (m_collapsed.contains(group.cls)) {
                m_collapsed.erase(group.cls);
            } else {
                m_collapsed.insert(group.cls);
            }
            rebuild();
            return;
        }
        const float boxX = x0 + kChevron + 4;
        if (p.x >= boxX - 2 && p.x < boxX + ui::Checkbox::kBox + 4) {
            toggleRow(row);
        }
    };
    m_table->onActivate = [this](int row) { toggleRow(row); };
    m_table->onSelect = [this](int) { detailChanged(); };
    m_table->tooltipFor = [this](int row) { return tooltipOf(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::DriverChip, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::DriversTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Drivers || change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            syncBootDetail(); // D-134: a storage driver queued for the image ends the warning
            m_table->refresh();
            m_imageTable->refresh();
            updateSummary();
            invalidate();
            detailChanged();
        } else if (change == AppState::Change::Source) {
            refresh(); // D-134: another medium, another boot.wim
        }
    });
    refresh();
}

DriversPage::~DriversPage() {
    m_state.unsubscribe(m_subscription);
}

void DriversPage::showImageTab() {
    m_tabs->setSelected(1);
    refresh();
}

void DriversPage::showBootTab() {
    m_tabs->setSelected(2);
    refresh();
}

void DriversPage::setArchFilterForDemo(int index) {
    m_archFilter = index;
    m_arch->setSelected(index);
    rebuild();
}

void DriversPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

bool DriversPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

bool DriversPage::queued(int inf) const {
    const auto& path = m_state.driverScan().infs[static_cast<std::size_t>(inf)].path;
    if (bootTab()) {
        return m_state.isBootDriver(path);
    }
    return m_state.changes().find(OpKind::AddDriver, path.wstring()) != nullptr;
}

void DriversPage::toggle(int inf) {
    const auto d = m_state.driverScan().infs[static_cast<std::size_t>(inf)]; // a copy: the tick rebuilds the rows
    if (bootTab()) {
        // A locked row (another architecture, in boot.wim already) only lets go of a tick a preset gave it.
        if (lockOf(inf) && !m_state.isBootDriver(d.path)) {
            return;
        }
        m_bootNote = m_images.toggleBoot(d) ? m_strings.format(Str::DriversBootAlsoImage, {{L"name", d.path.filename().wstring()}})
                                            : std::wstring();
        syncBootDetail();
        return;
    }
    if (!m_state.unqueue(OpKind::AddDriver, d.path.wstring())) {
        m_state.queue(ImageDriverController::addOperationFor(d));
    }
}

void DriversPage::toggleRow(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row r = m_rows[static_cast<std::size_t>(row)];
    const Group group = m_groups[static_cast<std::size_t>(r.group)]; // a copy: a boot tick rebuilds the rows
    if (group.media) {
        return; // D-134: what boot.wim has already is not ticked or unticked here
    }
    if (r.inf < 0) {
        toggleGroup(group);
    } else {
        toggle(r.inf);
    }
    // The row the user ticked draws its tick in (the INFs a class's box ticks with it stay instant).
    const bool on = r.inf < 0 ? std::ranges::any_of(group.infs, [&](int i) { return queued(i); }) : queued(r.inf);
    if (on && row < static_cast<int>(m_rows.size())) {
        m_table->revealCheck(row);
    }
}

void DriversPage::toggleGroup(const Group& group) {
    if (bootTab()) {
        // D-134: in one go (each tick used to rebuild the rows the loop walked); locked rows stay out.
        std::vector<core::DriverInf> open;
        bool all = true;
        for (const int i : group.infs) {
            if (!lockOf(i)) {
                open.push_back(m_state.driverScan().infs[static_cast<std::size_t>(i)]);
                all = all && queued(i);
            }
        }
        if (open.empty()) {
            return;
        }
        const int alsoImage = m_images.setBoot(open, !all);
        m_bootNote = alsoImage > 0 ? m_strings.format(Str::DriversBootAlsoImageN, {{L"n", std::to_wstring(alsoImage)}}) : std::wstring();
        syncBootDetail();
        return;
    }
    const bool all = std::ranges::all_of(group.infs, [&](int i) { return queued(i); });
    const auto batch = m_state.batchQueue(); // a class of 300 INFs: one notification, one undo step (audit PG-04)
    for (const int i : group.infs) {
        if (all || !queued(i)) {
            toggle(i);
        }
    }
}

void DriversPage::refresh() {
    // The boot tab needs no mounted image: boot.wim is patched when the ISO / USB is made.
    const bool boot = bootTab();
    if (boot) {
        m_images.loadMedia(); // D-134: what the medium's boot.wim has (once per source, reader thread)
        m_mediaArch = ImageDriverController::mediaArchitecture(m_state);
    }
    const bool mounted = m_state.mounted().has_value() || boot;
    const auto* media = boot ? mediaBoot() : nullptr;
    const bool any = !m_state.driverScan().infs.empty() || (media && !media->empty());
    m_tabs->setVisible(true);
    if (mounted && imageTab()) {
        // The image's own drivers.
        for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_class, m_arch, m_table}) {
            w->setVisible(false);
        }
        const auto& list = m_state.imageDrivers();
        const bool ready = list && list->status == AppState::ImageDrivers::Status::Ready;
        if (!list || list->status == AppState::ImageDrivers::Status::Loading) {
            // A new mount resets the list (AppState::setMounted): start reading it, as the other
            // per-mount pages do — nothing else would until the tab is switched.
            m_images.load();
            // D-139: a quiet line under the page's icon while the list is read (no spinner that never turns).
            m_empty->setContent(ui::icons::Icon::DriverChip, m_strings.get(Str::DriversImageLoading), m_strings.get(Str::DriversImageLoadingBody));
            m_empty->clearAction();
        } else if (list->status == AppState::ImageDrivers::Status::Failed) {
            m_empty->setContent(ui::icons::Icon::ErrorOctagon, m_strings.get(Str::DriversImageFailed), list->error.message);
            m_empty->setAction(m_strings.get(Str::CommonRetry)).onInvoke = [this] { m_images.load(true); };
        } else if (list->items.empty()) {
            m_empty->setContent(ui::icons::Icon::DriverChip, m_strings.get(Str::DriversImageNone), m_strings.get(Str::DriversImageNoneBody));
            m_empty->clearAction();
        }
        m_empty->setVisible(!ready || list->items.empty());
        m_imageTable->setVisible(ready && !list->items.empty());
        m_summaryLine->setVisible(m_imageTable->visible());
        m_imageTable->setRowCount(ready ? static_cast<int>(list->items.size()) : 0);
        updateSummary();
        layout();
        invalidate();
        detailChanged();
        return;
    }
    m_imageTable->setVisible(false);
    if (!mounted) {
        m_empty->setContent(ui::icons::Icon::DriverChip, m_strings.get(Str::DriversNoMountTitle),
                            m_strings.get(Str::DriversNoMountBody));
        m_empty->setAction(m_strings.get(Str::CommonGoImages)).onInvoke = m_intents.goImages;
    } else if (!any) {
        m_empty->setContent(ui::icons::Icon::OpenFolder, m_strings.get(Str::DriversEmptyTitle),
                            m_strings.get(boot ? Str::DriversBootEmptyBody : Str::DriversEmptyBody));
        m_empty->setAction(m_strings.get(Str::UpdatesScanFolder)).onInvoke = m_intents.scanFolder;
    }
    m_empty->setVisible(!mounted || !any);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_class, m_arch, m_summaryLine, m_table}) {
        w->setVisible(mounted && any);
    }
    // The class list is the scan's: a tick (Change::Drivers on the boot tab) keeps the filter the user set.
    if (m_classesFor != m_state.driverScan().infs.size()) {
        m_classesFor = m_state.driverScan().infs.size();
        std::vector<std::wstring> classes{m_strings.get(Str::CommonAll)};
        std::vector<std::wstring> seen;
        for (const auto& inf : m_state.driverScan().infs) {
            if (std::ranges::find(seen, inf.className) == seen.end()) {
                seen.push_back(inf.className);
                classes.push_back(className(inf.className, m_strings));
            }
        }
        m_class->setItems(std::move(classes), 0);
        m_classFilter = 0;
    }
    syncBootDetail();
    rebuild();
    updateSummary();
    detailChanged();
}

std::optional<Str> DriversPage::lockOf(int i) const {
    if (!bootTab() || i < 0 || i >= static_cast<int>(m_state.driverScan().infs.size())) {
        return std::nullopt;
    }
    const auto& inf = m_state.driverScan().infs[static_cast<std::size_t>(i)];
    if (!core::fitsArchitecture(inf, m_mediaArch)) {
        return Str::DriversBootWrongArch;
    }
    if (const auto* media = mediaBoot(); media && core::containsDriver(*media, inf)) {
        return Str::DriversBootInMedia;
    }
    return std::nullopt;
}

const std::vector<core::WimDriver>* DriversPage::mediaBoot() const {
    const auto& media = m_state.mediaDrivers();
    if (!media || media->status != AppState::MediaDrivers::Status::Ready || !m_state.source() ||
        media->source != m_state.source()->path) {
        return nullptr;
    }
    return &media->drivers.boot;
}

void DriversPage::syncBootDetail() {
    if (!bootTab()) {
        return;
    }
    const auto storageOnly = ImageDriverController::storageOnlyInBoot(m_state);
    m_bootDetailWarn = !storageOnly.empty();
    if (m_bootDetailWarn) {
        std::wstring names;
        for (const auto& p : storageOnly) {
            names += (names.empty() ? L"" : L", ") + p.filename().wstring();
        }
        m_bootDetail = m_strings.format(Str::DriversBootStorageOnly, {{L"names", names}});
    } else {
        m_bootDetail = m_bootNote.empty() ? m_strings.get(Str::DriversBootHint) : m_bootNote;
    }
    invalidate();
}

std::wstring DriversPage::tooltipOf(int row) const {
    if (!bootTab() || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.media >= 0) {
        const auto* media = mediaBoot();
        if (!media || r.media >= static_cast<int>(media->size())) {
            return {};
        }
        const auto& d = (*media)[static_cast<std::size_t>(r.media)];
        return m_strings.format(Str::DriversBootPresentTip, {{L"name", d.name()}, {L"published", d.publishedName}});
    }
    if (r.inf < 0) {
        return {};
    }
    const auto& inf = m_state.driverScan().infs[static_cast<std::size_t>(r.inf)];
    if (const auto lock = lockOf(r.inf); lock == Str::DriversBootWrongArch) {
        std::wstring archs;
        for (const auto& a : inf.architectures) {
            archs += (archs.empty() ? L"" : L", ") + (a == L"amd64" ? std::wstring(L"x64") : a);
        }
        return m_strings.format(Str::DriversBootWrongArchTip, {{L"arch", archs}, {L"media", core::architectureName(m_mediaArch)}});
    } else if (lock) {
        return m_strings.get(Str::DriversBootPresentScanTip);
    }
    switch (core::setupRole(inf)) {
    case core::SetupRole::Storage: return m_strings.get(Str::DriversRoleStorageTip);
    case core::SetupRole::Network: return m_strings.get(Str::DriversRoleNetworkTip);
    case core::SetupRole::Wireless: return m_strings.get(Str::DriversRoleWirelessTip);
    case core::SetupRole::Virtual: return m_strings.get(Str::DriversRoleVirtualTip);
    default: return m_strings.get(Str::DriversRoleNoneTip);
    }
}

void DriversPage::rebuild() {
    const auto& infs = m_state.driverScan().infs;
    std::vector<std::wstring> order;
    std::map<std::wstring, Group> groups;
    for (std::size_t i = 0; i < infs.size(); ++i) {
        const auto& inf = infs[i];
        if (!groups.contains(inf.className)) {
            order.push_back(inf.className);
            groups[inf.className].cls = inf.className;
        }
        auto& g = groups[inf.className];
        ++g.total;
        if (m_archFilter > 0 && !inf.supports(kArchFilterKeys[m_archFilter])) {
            continue;
        }
        if (!m_needle.empty() && wl::text::fold(inf.path.filename().wstring()).find(m_needle) == std::wstring::npos &&
            wl::text::fold(inf.provider).find(m_needle) == std::wstring::npos) {
            continue;
        }
        g.infs.push_back(static_cast<int>(i));
        g.size += inf.size;
    }
    m_groups.clear();
    m_rows.clear();
    // D-134: the boot tab opens with what the medium's boot.wim carries already.
    if (const auto* media = bootTab() ? mediaBoot() : nullptr; media && !media->empty()) {
        Group g;
        g.media = true;
        g.cls = kMediaGroupKey;
        g.total = static_cast<int>(media->size());
        m_groups.push_back(std::move(g));
        const int gi = static_cast<int>(m_groups.size()) - 1;
        m_rows.push_back({gi, -1});
        if (!m_collapsed.contains(m_groups.back().cls)) {
            for (std::size_t k = 0; k < media->size(); ++k) {
                const auto& d = (*media)[k];
                if (!m_needle.empty() && wl::text::fold(d.name()).find(m_needle) == std::wstring::npos &&
                    wl::text::fold(d.inf.provider).find(m_needle) == std::wstring::npos) {
                    continue;
                }
                Row row{gi, -1};
                row.media = static_cast<int>(k);
                m_rows.push_back(std::move(row));
            }
        }
    }
    for (std::size_t c = 0; c < order.size(); ++c) {
        if (m_classFilter > 0 && static_cast<int>(c) != m_classFilter - 1) {
            continue;
        }
        auto& g = groups[order[c]];
        if (g.infs.empty()) {
            continue;
        }
        m_groups.push_back(std::move(g));
        const int gi = static_cast<int>(m_groups.size()) - 1;
        m_rows.push_back({gi, -1});
        if (!m_needle.empty() || !m_collapsed.contains(m_groups.back().cls)) {
            for (const int i : m_groups.back().infs) {
                m_rows.push_back({gi, i});
            }
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    invalidate();
}

void DriversPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto& group = m_groups[static_cast<std::size_t>(r.group)];
    const bool isGroup = r.inf < 0 && r.media < 0;
    const auto* media = group.media ? mediaBoot() : nullptr;
    if (group.media && (!media || (r.media >= 0 && r.media >= static_cast<int>(media->size())))) {
        return; // the list went (another source) before the rows were rebuilt
    }
    const auto* inf = r.inf >= 0 ? &m_state.driverScan().infs[static_cast<std::size_t>(r.inf)] : nullptr;
    const auto* present = r.media >= 0 ? &(*media)[static_cast<std::size_t>(r.media)] : nullptr;
    if (!r.texts) {
        r.texts = true;
        if (isGroup && group.media) {
            r.name = m_strings.get(Str::DriversBootMediaGroup);
            r.detail = m_strings.format(Str::DriversInfN, {{L"n", std::to_wstring(media->size())}});
        } else if (isGroup) {
            r.name = className(group.cls, m_strings);
            r.detail = m_strings.format(Str::DriversInfN, {{L"n", std::to_wstring(group.infs.size())}});
            r.sizeText = formatBytes(group.size, m_language);
        } else {
            const core::DriverInf& d = inf ? *inf : present->inf;
            r.name = inf ? inf->path.filename().wstring() : present->name();
            r.detail = d.provider.empty() ? L"—" : d.provider;
            if (!d.version.empty()) {
                r.detail += L" · " + d.version;
            }
            r.sizeText = inf ? formatBytes(inf->size, m_language) : present->publishedName; // boot.wim's name for it
        }
    }
    switch (column) {
    case kName: {
        float x = rect.x + (isGroup ? 0.0f : kIndent);
        if (isGroup) {
            const bool open = !m_needle.empty() || !m_collapsed.contains(group.cls);
            canvas.drawIcon(open ? ui::icons::Icon::ChevronDown : ui::icons::Icon::ChevronRight, {x, rect.y + (rect.height - 16) / 2},
                            Color::TextTertiary);
        }
        x += kChevron + 4;
        if (const std::uint64_t revision = m_state.revision(); r.checkAt != revision) {
            r.checkAt = revision;
            ui::CheckState fresh = ui::CheckState::Off;
            r.locked = false;
            r.tag.clear();
            r.tagInk = Color::TextTertiary;
            if (group.media) {
                // D-134: in boot.wim already — on, and nothing here changes that.
                fresh = ui::CheckState::On;
                r.locked = true;
                if (!isGroup) {
                    r.tag = m_strings.get(Str::DriversBootInMedia);
                }
            } else if (isGroup) {
                const auto n = std::ranges::count_if(group.infs, [&](int i) { return queued(i); });
                fresh = n == 0 ? ui::CheckState::Off
                        : n == static_cast<std::ptrdiff_t>(group.infs.size()) ? ui::CheckState::On
                                                                              : ui::CheckState::Indeterminate;
                r.locked = bootTab() && std::ranges::all_of(group.infs, [&](int i) { return lockOf(i).has_value(); });
            } else {
                fresh = queued(r.inf) ? ui::CheckState::On : ui::CheckState::Off;
                if (bootTab()) {
                    // D-134: what the INF is for Setup, or why it cannot be ticked.
                    if (const auto lock = lockOf(r.inf); lock == Str::DriversBootWrongArch) {
                        r.locked = true;
                        std::wstring archs;
                        for (const auto& a : inf->architectures) {
                            archs += (archs.empty() ? L"" : L", ") + (a == L"amd64" ? std::wstring(L"x64") : a);
                        }
                        r.tag = m_strings.format(Str::DriversBootWrongArch, {{L"arch", archs}});
                        r.tagInk = Color::StatusWarning;
                    } else if (lock) {
                        r.locked = true;
                        r.tag = m_strings.get(Str::DriversBootInMedia);
                    } else {
                        switch (core::setupRole(*inf)) {
                        case core::SetupRole::Storage: r.tag = m_strings.get(Str::DriversRoleStorage); break;
                        case core::SetupRole::Network: r.tag = m_strings.get(Str::DriversRoleNetwork); break;
                        case core::SetupRole::Wireless:
                            r.tag = m_strings.get(Str::DriversRoleWireless);
                            r.tagInk = Color::StatusWarning;
                            break;
                        case core::SetupRole::Virtual: r.tag = m_strings.get(Str::DriversRoleVirtual); break;
                        default: break;
                        }
                    }
                }
            }
            r.check = static_cast<std::uint8_t>(fresh);
        }
        const auto state = static_cast<ui::CheckState>(r.check);
        ui::Checkbox::paintBox(canvas, {x, rect.y + (rect.height - ui::Checkbox::kBox) / 2}, state, cell.hoveredCell,
                               cell.checkReveal, r.locked);
        x += ui::Checkbox::kBox + 8;
        // D-114: a class row shows Windows' own class icon (setupapi.dll …), as Device Manager does.
        const auto& classInfo = core::deviceClassInfo({}, isGroup && !group.media ? group.cls : std::wstring());
        if (!isGroup || group.media || classInfo.iconFile.empty() ||
            !canvas.drawFileIcon(classInfo.iconFile, classInfo.iconIndex, RectF{x, rect.y + (rect.height - 16) / 2, 16, 16})) {
            const auto icon = group.media && isGroup ? ui::icons::Icon::ImageWim : isGroup ? classIcon(group.cls) : ui::icons::Icon::InfFile;
            canvas.drawIcon(icon, {x, rect.y + (rect.height - 16) / 2}, isGroup ? Color::AccentBase : Color::TextSecondary);
        }
        x += ui::tokens::size::icon + 6;
        // The tag at the right of the cell (the name is cut before it): no measuring in paint.
        const float tagRoom = r.tag.empty() ? 0.0f : std::min(kTagRoom, std::max(rect.right() - x, 0.0f) * 0.5f);
        canvas.drawText(r.name, {x, rect.y, std::max(rect.right() - x - tagRoom, 0.0f), rect.height},
                        isGroup ? kGroupStyle : cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        r.locked && !group.media ? Color::TextTertiary : Color::TextPrimary);
        if (!r.tag.empty()) {
            canvas.drawText(r.tag, {rect.right() - tagRoom, rect.y, tagRoom, rect.height}, TypeStyle::Caption, r.tagInk,
                            ui::TextAlign::Trailing);
        }
        break;
    }
    case kProvider:
        canvas.drawText(r.detail, rect, TypeStyle::Caption, isGroup ? Color::TextTertiary : Color::TextSecondary);
        break;
    case kSize:
        canvas.drawText(r.sizeText, rect, TypeStyle::Mono, present ? Color::TextTertiary : Color::TextPrimary, ui::TextAlign::Trailing);
        break;
    default: break;
    }
}

void DriversPage::layout() {
    const RectF b = bounds();
    m_tabs->setBounds({b.x, b.y + kTabsTop, b.width, kTabsHeight});
    const float below = b.y + kTabsTop + kTabsHeight + 11.0f;
    // The whole body, as on every page: its icon and sentence at the same height as theirs (the
    // tabs above stay usable; the empty state draws no background).
    m_empty->setBounds(b);
    {
        // The image's drivers: the line under the table is the detail card's when the card is beside it.
        const float top = below + kToolbar + 12;
        const float line = detailShown() ? 0.0f : kDetailLine;
        m_imageTable->setBounds({b.x, top, b.width, std::max(b.bottom() - top - line, 0.0f)});
        fitColumns(*m_imageTable, b.width, detailShown(), {kDate, kVersion, kImageProvider});
    }
    // D-139: the header row — search, Sınıf, Mimari, the summary at the right (on "İmajdaki sürücüler",
    // which has no filter, the summary alone, at the same place).
    const float y = b.y + kToolbarTop;
    float x = b.x;
    const float cap = detailShown() ? 160.0f : 240.0f;
    m_search->setWidth(cap);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_class, m_arch}) {
        const ui::SizeF size = w->measure({});
        const float width = std::min(size.width, cap);
        w->setBounds({x, y, width, kToolbar});
        x += width + kGap;
    }
    const float sx = imageTab() ? b.x : x + kHeaderGap - kGap;
    m_summaryLine->setBounds({sx, y, std::max(b.right() - sx, 0.0f), kToolbar});
    const float top = y + kToolbar + 12;
    const float detail = bootTab() ? kDetailLine : 0.0f;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top - detail, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kProvider});
}

void DriversPage::toggleImageRow(int row) {
    const auto& list = m_state.imageDrivers();
    if (list && row >= 0 && row < static_cast<int>(list->items.size())) {
        const auto driver = list->items[static_cast<std::size_t>(row)]; // a copy: the tick notifies
        m_images.toggle(driver);
        if (m_images.queuedForRemoval(driver)) {
            m_imageTable->revealCheck(row); // the user's tick draws in
        }
    }
}

RowMarks DriversPage::imageMarks(int row) const {
    const auto& list = m_state.imageDrivers();
    if (!list || row < 0 || row >= static_cast<int>(list->items.size())) {
        return {};
    }
    const auto& d = list->items[static_cast<std::size_t>(row)];
    return {m_images.queuedForRemoval(d) ? RowMark::Queued : RowMark::None, d.bootCritical ? core::ops::Risk::High : core::ops::Risk::Low};
}

void DriversPage::paintImageCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto& list = m_state.imageDrivers();
    if (!list || row < 0 || row >= static_cast<int>(list->items.size())) {
        return;
    }
    const auto& d = list->items[static_cast<std::size_t>(row)];
    switch (column) {
    case kDriver: {
        const bool remove = m_images.queuedForRemoval(d);
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                               remove ? ui::CheckState::On : ui::CheckState::Off, cell.hoveredCell, cell.checkReveal);
        float x = rect.x + ui::Checkbox::kBox + 8;
        const auto& info = core::deviceClassInfo({}, d.className);
        if (info.iconFile.empty() || !canvas.drawFileIcon(info.iconFile, info.iconIndex, RectF{x, rect.y + (rect.height - 16) / 2, 16, 16})) {
            canvas.drawIcon(classIcon(d.className), {x, rect.y + (rect.height - 16) / 2}, Color::TextSecondary);
        }
        x += ui::tokens::size::icon + 6;
        // D-139: only the name; "kaldırılacak" / "önyükleme için kritik" are the marks column's, the
        // published name (oemN.inf) the detail card's and the tooltip's.
        const std::wstring& name = d.originalFileName.empty() ? d.publishedName : d.originalFileName;
        canvas.drawText(name, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height}, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        remove ? Color::TextTertiary : Color::TextPrimary);
        break;
    }
    case kImageMarks: paintMarks(canvas, rect, imageMarks(row), kImageMarkSlots); break;
    case kClass:
        canvas.drawText(d.classDescription.empty() ? className(d.className, m_strings) : d.classDescription, rect,
                        TypeStyle::Caption, Color::TextSecondary);
        break;
    case kImageProvider: canvas.drawText(d.provider.empty() ? L"—" : d.provider, rect, TypeStyle::Caption, Color::TextSecondary); break;
    case kVersion: canvas.drawText(d.version, rect, TypeStyle::Mono, Color::TextPrimary); break;
    case kDate: canvas.drawText(d.date, rect, TypeStyle::Mono, Color::TextSecondary); break;
    default: break;
    }
}

void DriversPage::updateSummary() {
    // D-139: what the tab lists, quietly, then what it changes (in the accent) — at the header row's right.
    std::vector<SummaryPart> parts;
    if (imageTab()) {
        const auto& list = m_state.imageDrivers();
        const std::size_t n = list && list->status == AppState::ImageDrivers::Status::Ready ? list->items.size() : 0;
        parts.push_back({m_strings.format(Str::DriversSummaryImage, {{L"n", formatCount(n, m_language)}})});
        if (const std::size_t r = m_images.removalCount(); r > 0) {
            parts.push_back({m_strings.format(Str::ComponentsSummaryRemoveN, {{L"n", formatCount(r, m_language)}}), SummaryPart::Tone::Change});
        }
        m_summaryLine->set(std::move(parts));
        return;
    }
    const auto& scan = m_state.driverScan();
    if (bootTab()) {
        // D-134: what boot.wim has, then how many go in and by how much the medium grows (the INFs' folders).
        const auto& media = m_state.mediaDrivers();
        if (const auto* present = mediaBoot()) {
            parts.push_back({m_strings.format(Str::DriversSummaryPresent, {{L"n", formatCount(present->size(), m_language)}})});
        } else if (m_state.source() && (!media || media->status == AppState::MediaDrivers::Status::Loading)) {
            parts.push_back({m_strings.get(Str::DriversSummaryReading)});
        }
        if (!m_state.bootDrivers().empty()) {
            std::uint64_t grows = 0;
            for (const auto& path : m_state.bootDrivers()) {
                if (const auto it = std::ranges::find(scan.infs, path, &core::DriverInf::path); it != scan.infs.end()) {
                    grows += it->size;
                }
            }
            parts.push_back({m_strings.format(Str::DriversBootSummary, {{L"n", formatCount(m_state.bootDrivers().size(), m_language)},
                                                                        {L"size", formatBytes(grows, m_language)}}),
                             SummaryPart::Tone::Change});
        }
        m_summaryLine->set(std::move(parts));
        return;
    }
    std::uint64_t total = 0;
    std::uint64_t added = 0;
    std::size_t queuedN = 0;
    for (const auto& inf : scan.infs) {
        total += inf.size;
        if (m_state.changes().find(OpKind::AddDriver, inf.path.wstring())) {
            ++queuedN;
            added += inf.size;
        }
    }
    parts.push_back({m_strings.format(Str::DriversSummary, {{L"f", formatCount(scan.folders.size(), m_language)},
                                                            {L"n", formatCount(scan.infs.size(), m_language)},
                                                            {L"size", formatBytes(total, m_language)}})});
    if (queuedN > 0) {
        parts.push_back({m_strings.format(Str::DriversSummaryAdd, {{L"n", formatCount(queuedN, m_language)}, {L"size", formatBytes(added, m_language)}}),
                         SummaryPart::Tone::Change});
    }
    m_summaryLine->set(std::move(parts));
}

void DriversPage::paint(ui::Canvas& canvas) {
    if (m_imageTable->visible()) {
        const RectF b = bounds();
        if (!detailShown()) {
            paintDetail(canvas, {b.x, m_imageTable->bounds().bottom(), b.width, kDetailLine}, L"",
                        m_strings.get(Str::DriversImageHint));
        }
        return;
    }
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    if (bootTab()) {
        // D-134: when the drivers go in — or, in the warning ink, a storage driver the installed Windows lacks.
        const RectF line{b.x, m_table->bounds().bottom(), b.width, kDetailLine};
        if (m_bootDetailWarn) {
            canvas.hairlineH(line.x, line.y, line.width, Color::LineSubtle);
            canvas.drawIcon(ui::icons::Icon::WarningTriangle, {line.x, line.y + (line.height - 16) / 2}, Color::StatusWarning);
            canvas.drawText(m_bootDetail, {line.x + 22, line.y + 1, std::max(line.width - 22, 0.0f), line.height - 1}, TypeStyle::Caption,
                            Color::StatusWarning);
        } else {
            paintDetail(canvas, line, L"", m_bootDetail);
        }
    }
}

} // namespace wl::app

namespace wl::app {

bool DriversPage::detailAvailable() const {
    return imageTab() ? m_imageTable->visible() : m_table->visible();
}

DetailModel DriversPage::detail() const {
    DetailModel model;
    if (imageTab()) {
        // The image's own third-party driver.
        const auto& list = m_state.imageDrivers();
        const int row = m_imageTable->selected();
        if (!m_imageTable->visible() || !list || row < 0 || row >= static_cast<int>(list->items.size())) {
            return model;
        }
        const auto& d = list->items[static_cast<std::size_t>(row)];
        const bool remove = m_images.queuedForRemoval(d);
        model.icon = classIcon(d.className);
        model.title = d.originalFileName.empty() ? d.publishedName : d.originalFileName;
        model.subtitle = d.classDescription.empty() ? className(d.className, m_strings) : d.classDescription;
        model.identity = d.publishedName;
        model.state = remove ? DetailNote{ui::icons::Icon::QueueClock, m_strings.get(Str::ComponentsWillRemove), DetailTone::Change}
                             : DetailNote{ui::icons::Icon::Check, m_strings.get(Str::ComponentsStays), DetailTone::Quiet};
        model.facts.push_back({m_strings.get(Str::DriversProviderName), d.provider.empty() ? std::wstring(L"—") : d.provider});
        model.facts.push_back({m_strings.get(Str::DriversVersion), d.version, DetailTone::Normal, true});
        model.facts.push_back({m_strings.get(Str::DriversDate), d.date, DetailTone::Normal, true});
        model.facts.push_back({m_strings.get(Str::DriversPublished), d.publishedName, DetailTone::Normal, true});
        model.facts.push_back({m_strings.get(Str::DriversSigned), m_strings.get(d.signed_ ? Str::CommonYes : Str::CommonNo)});
        if (d.bootCritical) {
            model.facts.push_back({m_strings.get(Str::DriversBootFact), m_strings.get(Str::DriversBootCritical), DetailTone::Error, false, true});
            model.notes.push_back({ui::icons::Icon::WarningTriangle, m_strings.get(Str::DriversImageHint), DetailTone::Warning});
        }
        model.actions.push_back({kActionToggle, m_strings.get(remove ? Str::DriversActionDrop : Str::DriversActionRemove)});
        return model;
    }
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return model;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto& group = m_groups[static_cast<std::size_t>(r.group)];
    const bool boot = bootTab();
    if (r.inf < 0 && r.media < 0) {
        // A class (or the medium's boot.wim group): what is under it and its box's action.
        model.icon = group.media ? ui::icons::Icon::ImageWim : classIcon(group.cls);
        model.title = group.media ? m_strings.get(Str::DriversBootMediaGroup) : className(group.cls, m_strings);
        const std::size_t n = group.media ? static_cast<std::size_t>(group.total) : group.infs.size();
        const auto on = group.media ? n : static_cast<std::size_t>(std::ranges::count_if(group.infs, [&](int i) { return queued(i); }));
        model.facts.push_back({m_strings.get(Str::DriversDriver),
                               m_strings.format(Str::DriversSelected, {{L"on", formatCount(on, m_language)}, {L"n", formatCount(n, m_language)}})});
        if (group.size > 0) {
            model.facts.push_back({m_strings.get(Str::CommonSize), formatBytes(group.size, m_language), DetailTone::Normal, true});
        }
        if (!group.media) {
            model.actions.push_back({kActionToggle, m_strings.get(on == n ? Str::DriversGroupDrop : Str::DriversGroupAdd)});
        }
        return model;
    }
    if (r.media >= 0) {
        // What the medium's boot.wim carries already.
        const auto* media = mediaBoot();
        if (!media || r.media >= static_cast<int>(media->size())) {
            return model;
        }
        const auto& d = (*media)[static_cast<std::size_t>(r.media)];
        model.icon = ui::icons::Icon::InfFile;
        model.title = d.name();
        model.identity = d.publishedName;
        model.state = {ui::icons::Icon::Lock, m_strings.get(Str::DriversStateInMedia), DetailTone::Quiet};
        model.facts.push_back({m_strings.get(Str::DriversProviderName), d.inf.provider.empty() ? std::wstring(L"—") : d.inf.provider});
        model.facts.push_back({m_strings.get(Str::DriversVersion), d.inf.version, DetailTone::Normal, true});
        return model;
    }
    const auto& inf = m_state.driverScan().infs[static_cast<std::size_t>(r.inf)];
    const bool on = queued(r.inf);
    const auto lock = lockOf(r.inf);
    model.icon = ui::icons::Icon::InfFile;
    model.title = inf.path.filename().wstring();
    model.subtitle = className(inf.className, m_strings);
    model.identity = inf.path.parent_path().wstring();
    if (on) {
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(boot ? Str::DriversStateBoot : Str::DriversStateAdd), DetailTone::Change};
    } else if (lock) {
        model.state = {ui::icons::Icon::Lock, m_strings.get(*lock == Str::DriversBootInMedia ? Str::DriversStateInMedia : Str::MarksLocked),
                       DetailTone::Quiet};
    } else {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::DriversStateNot), DetailTone::Quiet};
    }
    if (boot) {
        model.about = tooltipOf(row); // what the INF is for Setup, or why it cannot go in
    }
    model.facts.push_back({m_strings.get(Str::DriversProviderName), inf.provider.empty() ? std::wstring(L"—") : inf.provider});
    model.facts.push_back({m_strings.get(Str::DriversVersion), inf.version, DetailTone::Normal, true});
    if (!inf.date.empty()) {
        model.facts.push_back({m_strings.get(Str::DriversDate), inf.date, DetailTone::Normal, true});
    }
    std::wstring archs;
    for (const auto& a : inf.architectures) {
        archs += (archs.empty() ? L"" : L", ") + (a == L"amd64" ? std::wstring(L"x64") : a);
    }
    model.facts.push_back({m_strings.get(Str::ImagesArch), archs.empty() ? std::wstring(L"—") : archs, DetailTone::Normal, true});
    model.facts.push_back({m_strings.get(Str::CommonSize), formatBytes(inf.size, m_language), DetailTone::Normal, true});
    if (!inf.catalog.empty()) {
        model.facts.push_back({m_strings.get(Str::DriversCatalogFact), inf.catalog, DetailTone::Normal, true});
    }
    if (!inf.devices.empty()) {
        model.lists.push_back({m_strings.get(Str::DetailDevices) + L" · " + formatCount(inf.devices.size(), m_language), inf.devices});
    }
    if (!lock || on) {
        model.actions.push_back({kActionToggle, m_strings.get(on ? Str::DriversActionDrop : Str::DriversActionAdd)});
    }
    return model;
}

void DriversPage::detailAction(int id) {
    if (id != kActionToggle) {
        return;
    }
    if (imageTab()) {
        toggleImageRow(m_imageTable->selected()); // the row's own Space / double click
    } else {
        toggleRow(m_table->selected());
    }
}

} // namespace wl::app
