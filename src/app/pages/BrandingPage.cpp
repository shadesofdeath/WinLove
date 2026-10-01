#include "app/pages/BrandingPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "ui/widgets/Button.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 8.0f;
constexpr float kLabelWidth = 180.0f;
constexpr float kFieldWidth = 360.0f;
constexpr float kFormHeight = ui::FormView::kSection * 2 + ui::FormView::kRow * 9 + 8;
constexpr float kFontsTitle = 32.0f;
enum FontColumn : int { kFontName, kFontFile, kFontSize, kFontRemove };
constexpr std::array<Str, 5> kOemLabels{Str::BrandingManufacturer, Str::BrandingModel, Str::BrandingSupportPhone,
                                        Str::BrandingSupportHours, Str::BrandingSupportUrl};
} // namespace

// "Seç…" · file name / "Windows varsayılanı" · ×
class BrandingPictureField : public ui::Widget {
public:
    BrandingPictureField(std::wstring choose, std::wstring defaultText, std::wstring clearTooltip)
        : m_default(std::move(defaultText)) {
        m_choose = &add<ui::Button>(ui::ButtonKind::Secondary, std::move(choose));
        m_choose->onInvoke = [this] {
            if (onChoose) {
                onChoose();
            }
        };
        auto clear = ui::Button::iconOnly(ui::icons::Icon::Close, std::move(clearTooltip));
        m_clear = clear.get();
        addChild(std::move(clear));
        m_clear->onInvoke = [this] {
            if (onClear) {
                onClear();
            }
        };
        m_clear->setVisible(false);
    }
    std::function<void()> onChoose;
    std::function<void()> onClear;

    void setFile(const std::optional<std::filesystem::path>& file) {
        m_file = file ? file->filename().wstring() : std::wstring();
        m_tooltipPath = file ? file->wstring() : std::wstring();
        setTooltip(m_tooltipPath);
        m_clear->setVisible(file.has_value());
        invalidate();
    }

    [[nodiscard]] ui::SizeF measure(ui::SizeF) override { return {kFieldWidth, ui::tokens::size::control}; }
    void layout() override {
        const RectF b = bounds();
        const ui::SizeF size = m_choose->measure({});
        m_choose->setBounds({b.x, b.y, size.width, b.height});
        m_clear->setBounds({b.right() - b.height, b.y, b.height, b.height});
    }
    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        const float x = m_choose->bounds().right() + 10;
        const float right = m_clear->visible() ? m_clear->bounds().x - 6 : b.right();
        canvas.drawText(m_file.empty() ? m_default : m_file, {x, b.y, std::max(right - x, 0.0f), b.height},
                        m_file.empty() ? TypeStyle::Body : TypeStyle::BodyStrong,
                        m_file.empty() ? Color::TextTertiary : Color::TextPrimary);
    }

private:
    ui::Button* m_choose = nullptr;
    ui::Button* m_clear = nullptr;
    std::wstring m_default;
    std::wstring m_file;
    std::wstring m_tooltipPath;
};

BrandingPage::BrandingPage(AppState& state, BrandingController& controller, const Localization& strings,
                           Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_form = &add<ui::FormView>(kLabelWidth);

    m_form->addSection(s(Str::BrandingOem));
    for (std::size_t i = 0; i < kOemLabels.size(); ++i) {
        auto& box = m_form->addRow<ui::SearchBox>(s(kOemLabels[i]), std::wstring(), kFieldWidth, std::wstring());
        box.setPlain(true);
        box.setAccessible(ui::AccessRole::Edit, s(kOemLabels[i]));
        const std::wstring field(core::kOemFields[i]);
        box.onChange = [this, field](const std::wstring& text) { m_controller.setOem(field, text); };
        m_oem[i] = &box;
    }
    auto picture = [&](core::PictureSlot slot, Str label, Str hint) {
        auto& field = m_form->addRow<BrandingPictureField>(s(label), s(hint), 0.0f, s(Str::BrandingChoose), s(Str::BrandingDefault),
                                                   s(Str::BrandingClear));
        field.setAccessible(ui::AccessRole::Group, s(label));
        field.onChoose = [this, slot] {
            if (!m_intents.pickPicture) {
                return;
            }
            if (const auto file = m_intents.pickPicture()) {
                if (auto set = m_controller.setPicture(slot, *file); !set && m_intents.refused) {
                    m_intents.refused(file->filename().wstring());
                }
            }
        };
        field.onClear = [this, slot] { m_controller.clearPicture(slot); };
        m_pictures[static_cast<std::size_t>(slot)] = &field;
    };
    picture(core::PictureSlot::OemLogo, Str::BrandingLogo, Str::BrandingLogoHint);
    m_form->addSection(s(Str::BrandingPictures));
    picture(core::PictureSlot::Wallpaper, Str::BrandingWallpaper, Str::BrandingWallpaperHint);
    picture(core::PictureSlot::LockScreen, Str::BrandingLockScreen, Str::BrandingLockScreenHint);
    picture(core::PictureSlot::Account, Str::BrandingAccount, Str::BrandingAccountHint);

    m_fonts = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {s(Str::BrandingFont), 0},
        {s(Str::BrandingFontFile), 240},
        {s(Str::CommonSize), 96, ui::TextAlign::Trailing},
        {L"", 32},
    });
    m_fonts->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintFont(c, row, column, rect, cell);
    };
    m_fonts->onCellClick = [this](int row, int column, ui::PointF) {
        if (column == kFontRemove && row >= 0 && row < static_cast<int>(m_fontRows.size())) {
            m_controller.removeFont(m_fontRows[static_cast<std::size_t>(row)].file);
        }
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::WindowsLogoGeneric, s(Str::BrandingNoMountTitle), s(Str::BrandingNoMountBody));
    m_empty->setAction(s(Str::CommonGoImages)).onInvoke = [this] {
        if (m_intents.goImages) {
            m_intents.goImages();
        }
    };
    setAccessible(ui::AccessRole::Group, s(Str::BrandingTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Queue || change == AppState::Change::Mount) {
            refresh();
        }
    });
    refresh();
}

BrandingPage::~BrandingPage() {
    m_state.unsubscribe(m_subscription);
}

void BrandingPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_form->setVisible(mounted);
    m_fonts->setVisible(mounted);
    for (std::size_t i = 0; i < m_oem.size(); ++i) {
        // Not under the caret: the box being typed in already shows what it queued.
        if (!m_oem[i]->focused()) {
            const std::wstring text = m_controller.oem(core::kOemFields[i]);
            if (m_oem[i]->text() != text) {
                m_oem[i]->setText(text);
            }
        }
    }
    for (const auto slot : {core::PictureSlot::Wallpaper, core::PictureSlot::LockScreen, core::PictureSlot::Account,
                            core::PictureSlot::OemLogo}) {
        m_pictures[static_cast<std::size_t>(slot)]->setFile(m_controller.picture(slot));
    }
    m_fontRows = m_controller.fonts();
    m_fonts->setRowCount(static_cast<int>(m_fontRows.size()));
    m_fonts->refresh();
    invalidate();
}

void BrandingPage::paintFont(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_fontRows.size())) {
        return;
    }
    const auto& f = m_fontRows[static_cast<std::size_t>(row)];
    switch (column) {
    case kFontName:
        canvas.drawIcon(ui::icons::Icon::File, {rect.x, rect.y + 4}, Color::TextSecondary);
        canvas.drawText(f.name, {rect.x + ui::tokens::size::icon + 6, rect.y, rect.width - ui::tokens::size::icon - 6, rect.height},
                        cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    case kFontFile: canvas.drawText(f.file, rect, TypeStyle::Mono, Color::TextSecondary); break;
    case kFontSize:
        canvas.drawText(formatBytes(f.size, m_language), rect, TypeStyle::Mono, Color::TextPrimary, ui::TextAlign::Trailing);
        break;
    case kFontRemove:
        canvas.drawIcon(ui::icons::Icon::Close, {rect.x, rect.y + 4}, cell.hoveredCell ? Color::TextPrimary : Color::TextTertiary);
        break;
    default: break;
    }
}

void BrandingPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    const float formHeight = std::min(kFormHeight, std::max(b.height * 0.62f, 0.0f));
    m_form->setBounds({b.x, b.y + kTop, b.width, formHeight});
    const float top = b.y + kTop + formHeight + kFontsTitle;
    m_fonts->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
}

void BrandingPage::paint(ui::Canvas& canvas) {
    if (!m_fonts->visible()) {
        return;
    }
    const RectF t = m_fonts->bounds();
    const RectF title{t.x, t.y - kFontsTitle, t.width, kFontsTitle - 6};
    canvas.drawText(m_strings.get(Str::BrandingFonts), title, TypeStyle::Caption, Color::TextSecondary);
    const std::wstring summary = m_fontRows.empty() ? m_strings.get(Str::BrandingFontsEmpty)
                                                    : m_strings.format(Str::BrandingFontsN, {{L"n", std::to_wstring(m_fontRows.size())}});
    canvas.drawText(summary, title, TypeStyle::Caption, Color::TextTertiary, ui::TextAlign::Trailing);
}

} // namespace wl::app
