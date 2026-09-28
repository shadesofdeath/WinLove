#include "app/pages/DriversPage.h"

#include "app/Format.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <cwctype>
#include <map>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 8.0f;
constexpr float kIndent = 16.0f;
constexpr float kChevron = 16.0f;
enum Column : int { kName, kProvider, kSize };

std::wstring lowered(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

const wchar_t* kArchKeys[] = {L"", L"x64", L"arm64", L"x86"};

ui::icons::Icon classIcon(const std::wstring& cls) {
    const std::wstring c = lowered(cls);
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

std::wstring DriversPage::className(const std::wstring& cls, Language language) {
    static const std::map<std::wstring, std::pair<const wchar_t*, const wchar_t*>> kNames{
        {L"net", {L"Ağ", L"Network"}},
        {L"scsiadapter", {L"Depolama", L"Storage"}},
        {L"hdc", {L"Disk denetleyicisi", L"Disk controller"}},
        {L"display", {L"Ekran", L"Display"}},
        {L"system", {L"Yonga seti", L"Chipset"}},
        {L"media", {L"Ses ve video", L"Sound and video"}},
        {L"usb", {L"USB", L"USB"}},
        {L"bluetooth", {L"Bluetooth", L"Bluetooth"}},
        {L"hidclass", {L"Giriş aygıtları", L"Input devices"}},
        {L"camera", {L"Kamera", L"Camera"}},
        {L"printer", {L"Yazıcı", L"Printer"}},
        {L"extension", {L"Uzantı", L"Extension"}},
        {L"softwarecomponent", {L"Yazılım bileşeni", L"Software component"}},
        {L"firmware", {L"Donanım yazılımı", L"Firmware"}},
    };
    if (cls.empty()) {
        return language == Language::Turkish ? L"Sınıfsız" : L"No class";
    }
    const auto it = kNames.find(lowered(cls));
    if (it == kNames.end()) {
        return cls;
    }
    return std::wstring(language == Language::Turkish ? it->second.first : it->second.second) + L" (" + cls + L")";
}

DriversPage::DriversPage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::DriversSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = lowered(text);
        rebuild();
    };
    m_class = &add<ui::Dropdown>(strings.get(Str::DriversClass), std::vector<std::wstring>{strings.get(Str::CommonAll)}, 0);
    m_class->onChange = [this](int index) {
        m_classFilter = index;
        rebuild();
    };
    m_arch = &add<ui::Dropdown>(strings.get(Str::ImagesArch),
                                std::vector<std::wstring>{strings.get(Str::CommonAll), L"x64", L"arm64", L"x86"}, 0);
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
                    if (arch == kArchKeys[i]) {
                        m_archFilter = i;
                        m_arch->setSelected(i);
                    }
                }
            }
        }
    }
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::CommonName), 0},
        {strings.get(Str::DriversProvider), 260},
        {strings.get(Str::CommonSize), 96, ui::TextAlign::Trailing},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column != kName || row < 0 || row >= static_cast<int>(m_rows.size())) {
            return;
        }
        const Row& r = m_rows[static_cast<std::size_t>(row)];
        const auto& group = m_groups[static_cast<std::size_t>(r.group)];
        const RectF cell = m_table->cellRect(row, column);
        const float x0 = cell.x + ui::TableView::kCellPad + (r.inf < 0 ? 0.0f : kIndent);
        if (r.inf < 0 && p.x < x0 + kChevron) {
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
            if (r.inf < 0) {
                toggleGroup(group);
            } else {
                toggle(r.inf);
            }
        }
    };
    m_table->onActivate = [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_rows.size())) {
            return;
        }
        const Row& r = m_rows[static_cast<std::size_t>(row)];
        if (r.inf < 0) {
            toggleGroup(m_groups[static_cast<std::size_t>(r.group)]);
        } else {
            toggle(r.inf);
        }
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::DriverChip, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::DriversTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Drivers || change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            m_table->refresh();
            invalidate();
        }
    });
    refresh();
}

DriversPage::~DriversPage() {
    m_state.unsubscribe(m_subscription);
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
    return m_state.changes().find(OpKind::AddDriver, m_state.driverScan().infs[static_cast<std::size_t>(inf)].path.wstring()) !=
           nullptr;
}

void DriversPage::toggle(int inf) {
    const auto& d = m_state.driverScan().infs[static_cast<std::size_t>(inf)];
    if (!m_state.unqueue(OpKind::AddDriver, d.path.wstring())) {
        core::ops::Operation op{OpKind::AddDriver, d.path.wstring(), d.className};
        op.risk = core::ops::Risk::Low;
        op.sizeDelta = static_cast<std::int64_t>(d.size);
        m_state.queue(std::move(op));
    }
}

void DriversPage::toggleGroup(const Group& group) {
    const bool all = std::ranges::all_of(group.infs, [&](int i) { return queued(i); });
    for (const int i : group.infs) {
        if (all || !queued(i)) {
            toggle(i);
        }
    }
}

void DriversPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    const bool any = !m_state.driverScan().infs.empty();
    if (!mounted) {
        m_empty->setContent(ui::icons::Icon::DriverChip, m_strings.get(Str::DriversNoMountTitle),
                            m_strings.get(Str::DriversNoMountBody));
        m_empty->setAction(m_strings.get(Str::FeaturesGoImages)).onInvoke = m_intents.goImages;
    } else if (!any) {
        m_empty->setContent(ui::icons::Icon::OpenFolder, m_strings.get(Str::DriversEmptyTitle),
                            m_strings.get(Str::DriversEmptyBody));
        m_empty->setAction(m_strings.get(Str::UpdatesScanFolder)).onInvoke = m_intents.scanFolder;
    }
    m_empty->setVisible(!mounted || !any);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_class, m_arch, m_table}) {
        w->setVisible(mounted && any);
    }
    std::vector<std::wstring> classes{m_strings.get(Str::CommonAll)};
    std::vector<std::wstring> seen;
    for (const auto& inf : m_state.driverScan().infs) {
        if (std::ranges::find(seen, inf.className) == seen.end()) {
            seen.push_back(inf.className);
            classes.push_back(className(inf.className, m_language));
        }
    }
    m_class->setItems(std::move(classes), 0);
    m_classFilter = 0;
    rebuild();
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
        if (m_archFilter > 0 && !inf.supports(kArchKeys[m_archFilter])) {
            continue;
        }
        if (!m_needle.empty() && lowered(inf.path.filename().wstring()).find(m_needle) == std::wstring::npos &&
            lowered(inf.provider).find(m_needle) == std::wstring::npos) {
            continue;
        }
        g.infs.push_back(static_cast<int>(i));
        g.size += inf.size;
    }
    m_groups.clear();
    m_rows.clear();
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
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto& group = m_groups[static_cast<std::size_t>(r.group)];
    const bool isGroup = r.inf < 0;
    const auto* inf = isGroup ? nullptr : &m_state.driverScan().infs[static_cast<std::size_t>(r.inf)];
    switch (column) {
    case kName: {
        float x = rect.x + (isGroup ? 0.0f : kIndent);
        if (isGroup) {
            const bool open = !m_needle.empty() || !m_collapsed.contains(group.cls);
            canvas.drawIcon(open ? ui::icons::Icon::ChevronDown : ui::icons::Icon::ChevronRight, {x, rect.y + 4},
                            Color::TextTertiary);
        }
        x += kChevron + 4;
        ui::CheckState state = ui::CheckState::Off;
        if (isGroup) {
            const auto n = std::ranges::count_if(group.infs, [&](int i) { return queued(i); });
            state = n == 0 ? ui::CheckState::Off
                    : n == static_cast<std::ptrdiff_t>(group.infs.size()) ? ui::CheckState::On
                                                                          : ui::CheckState::Indeterminate;
        } else if (queued(r.inf)) {
            state = ui::CheckState::On;
        }
        ui::Checkbox::paintBox(canvas, {x, rect.y + (rect.height - ui::Checkbox::kBox) / 2}, state, cell.hoveredCell);
        x += ui::Checkbox::kBox + 8;
        canvas.drawIcon(isGroup ? classIcon(group.cls) : ui::icons::Icon::InfFile, {x, rect.y + 4}, Color::TextSecondary);
        x += ui::tokens::size::icon + 6;
        canvas.drawText(isGroup ? className(group.cls, m_language) : inf->path.filename().wstring(),
                        {x, rect.y, rect.right() - x, rect.height}, isGroup || cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        Color::TextPrimary);
        break;
    }
    case kProvider:
        if (isGroup) {
            canvas.drawText(m_strings.format(Str::DriversInfN, {{L"n", std::to_wstring(group.infs.size())}}), rect,
                            TypeStyle::Caption, Color::TextTertiary);
        } else {
            std::wstring text = inf->provider.empty() ? L"—" : inf->provider;
            if (!inf->version.empty()) {
                text += L" · " + inf->version;
            }
            canvas.drawText(text, rect, TypeStyle::Caption, Color::TextSecondary);
        }
        break;
    case kSize: {
        const std::uint64_t size = isGroup ? group.size : inf->size;
        canvas.drawText(formatBytes(size, m_language), rect, TypeStyle::Mono, Color::TextPrimary, ui::TextAlign::Trailing);
        break;
    }
    default: break;
    }
}

void DriversPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    const float y = b.y + kToolbarTop;
    float x = b.x;
    m_search->setWidth(240);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_class, m_arch}) {
        const ui::SizeF size = w->measure({});
        const float width = std::min(size.width, 240.0f);
        w->setBounds({x, y, width, kToolbar});
        x += width + kGap;
    }
    const float top = y + kToolbar + 12;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
}

void DriversPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const auto& scan = m_state.driverScan();
    std::uint64_t total = 0;
    for (const auto& inf : scan.infs) {
        total += inf.size;
    }
    const std::wstring text = m_strings.format(Str::DriversSummary, {{L"f", std::to_wstring(scan.folders.size())},
                                                                     {L"n", std::to_wstring(scan.infs.size())},
                                                                     {L"size", formatBytes(total, m_language)}});
    const float left = m_arch->bounds().right() + kGap;
    canvas.drawText(text, {left, b.y + kToolbarTop, std::max(b.right() - left, 0.0f), kToolbar}, TypeStyle::Caption,
                    Color::TextSecondary, ui::TextAlign::Trailing);
}

} // namespace wl::app
