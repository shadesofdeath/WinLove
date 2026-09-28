#include "app/pages/SourcePage.h"

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
// Offsets under the page description, from screen 01 (content top 88).
constexpr float kTopGap = 20.0f;
constexpr float kZoneHeight = 160.0f;
constexpr float kGap = 16.0f;
constexpr float kSectionGap = 24.0f;
constexpr float kSectionLine = 16.0f;
constexpr float kSectionToTable = 8.0f;
constexpr float kInfoBarHeight = 32.0f;
} // namespace

SourcePage::SourcePage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_intents(std::move(intents)) {
    m_drop = &add<ui::DropZone>(strings.get(Str::SourceDropTitle), strings.get(Str::SourceDropHint),
                                strings.get(Str::SourceDropUnsupported), strings.get(Str::SourceLoading));
    m_drop->onInvoke = [this] {
        if (m_intents.pickFile) {
            m_intents.pickFile();
        }
    };
    m_error = &add<ui::InfoBar>(ui::InfoKind::Error, strings.get(Str::SourceOpenFailed), L"", strings.get(Str::CommonClose));
    m_error->setVisible(false);
    m_error->onClose = [this] {
        m_error->setVisible(false);
        layout();
    };
    m_recent = &add<RecentList>(strings, language);
    m_recent->onOpen = [this](const std::filesystem::path& path) {
        if (m_intents.openPath) {
            m_intents.openPath(path);
        }
    };
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Recent) {
            refreshRecent();
        }
    });
    refreshRecent();
}

SourcePage::~SourcePage() {
    m_state.unsubscribe(m_subscription);
}

void SourcePage::refreshRecent() {
    m_recent->setEntries(m_state.recent().entries());
    m_recent->setVisible(!m_recent->empty());
    layout();
}

void SourcePage::setLoading(bool loading) {
    m_drop->setLoading(loading);
    if (loading) {
        m_error->setVisible(false);
        layout();
    }
}

void SourcePage::showError(const std::wstring& message) {
    m_error->set(ui::InfoKind::Error, m_strings.get(Str::SourceOpenFailed), message);
    m_error->setVisible(true);
    layout();
}

void SourcePage::setDragState(ui::DropZone::DragState state) {
    m_drop->setDragState(state);
}

void SourcePage::layout() {
    const RectF b = bounds();
    float y = b.y + kTopGap;
    m_drop->setBounds({b.x, y, b.width, kZoneHeight}); // live-system card removed (D-021): full width
    y += kZoneHeight;
    if (m_error->visible()) {
        y += kGap;
        m_error->setBounds({b.x, y, b.width, kInfoBarHeight});
        y += kInfoBarHeight;
    }
    y += kSectionGap + kSectionLine + kSectionToTable;
    m_recent->setBounds({b.x, y, b.width, m_recent->contentHeight()});
}

void SourcePage::paint(ui::Canvas& canvas) {
    if (m_recent->visible()) {
        const RectF r = m_recent->bounds();
        canvas.drawText(m_strings.get(Str::SourceRecent), {r.x, r.y - kSectionToTable - kSectionLine, r.width, kSectionLine},
                        TypeStyle::Section, Color::TextSecondary);
    }
}

} // namespace wl::app
