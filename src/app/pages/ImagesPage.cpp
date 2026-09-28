#include "app/pages/ImagesPage.h"

#include "app/Format.h"

#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 12.0f;
constexpr float kInfoBar = 32.0f;
} // namespace

ImagesPage::ImagesPage(AppState& state, ImageController& controller, const Localization& strings, Language language,
                       std::function<void()> chooseSource)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::LayersEditions, strings.get(Str::ImagesEmptyTitle),
                                   strings.get(Str::ImagesEmptyBody));
    m_empty->setAction(strings.get(Str::ImagesEmptyAction)).onInvoke = std::move(chooseSource);

    m_error = &add<ui::InfoBar>(ui::InfoKind::Error, L"", L"", strings.get(Str::CommonClose));
    m_error->setVisible(false);
    m_error->onClose = [this] {
        m_error->setVisible(false);
        layout();
    };

    m_strip = &add<OperationStrip>(strings, state);
    m_strip->setVisible(false);
    m_strip->onCancel = [this] { m_controller.cancel(); };

    m_table = &add<EditionTable>(strings, language);
    m_table->onSelect = [this](int index) { m_state.select(index); };
    m_table->onActivate = [this](int index) {
        if (m_controller.canMount()) {
            m_controller.mount(index);
        }
    };

    m_subscription = m_state.subscribe([this](AppState::Change change) { refresh(change); });
    refresh(AppState::Change::Source);
}

ImagesPage::~ImagesPage() {
    m_state.unsubscribe(m_subscription);
}

void ImagesPage::showFailure(const std::wstring& title, const std::wstring& message, bool offerCleanup) {
    m_error->set(ui::InfoKind::Error, title, message);
    if (offerCleanup) {
        m_error->setAction(m_strings.get(Str::ImagesCleanupMounts), [this] {
            m_error->setVisible(false);
            m_controller.cleanupMounts();
        });
    } else {
        m_error->setAction(L"", nullptr);
    }
    m_error->setVisible(true);
    layout();
}

void ImagesPage::refresh(AppState::Change change) {
    const auto& source = m_state.source();
    m_empty->setVisible(!source);
    m_table->setVisible(source.has_value());
    if (change == AppState::Change::Source) {
        m_table->setImages(source ? source->install.images : std::vector<core::ImageInfo>{});
    }
    m_table->setSelected(m_state.selectedIndex());

    const auto& op = m_state.operation();
    const bool wasRunning = m_strip->visible();
    m_strip->setVisible(op.has_value());
    if (op && !wasRunning) {
        m_strip->start();
        m_error->setVisible(false);
    }
    if (op) {
        m_table->setRowState(op->index, EditionTable::RowState::Working, true);
    } else if (const auto& mounted = m_state.mounted()) {
        m_table->setRowState(mounted->index, EditionTable::RowState::Mounted, false);
    } else if (const auto failed = m_controller.failedIndex()) {
        m_table->setRowState(*failed, EditionTable::RowState::Failed, false);
    } else {
        m_table->setRowState(std::nullopt, EditionTable::RowState::Normal, false);
    }
    layout();
    invalidate();
}

void ImagesPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kToolbarTop + kToolbar + kGap;
    if (m_error->visible()) {
        m_error->setBounds({b.x, y, b.width, kInfoBar});
        y += kInfoBar + kGap;
    }
    if (m_strip->visible()) {
        m_strip->setBounds({b.x, y, b.width, OperationStrip::kHeight});
        y += OperationStrip::kHeight + kGap;
    }
    m_table->setBounds({b.x, y, b.width, m_table->contentHeight()});
}

void ImagesPage::paint(ui::Canvas& canvas) {
    const auto& source = m_state.source();
    if (!source) {
        return;
    }
    // Toolbar row: summary on the right ("install.wim · 6 index · 6,72 GB").
    const RectF b = bounds();
    const std::wstring summary = m_strings.format(
        Str::ImagesSummary, {{L"file", std::filesystem::path(source->installImage).filename().wstring()},
                             {L"n", std::to_wstring(source->install.images.size())},
                             {L"size", formatBytes(source->installImageSize, m_language)}});
    canvas.drawText(summary, {b.x, b.y + kToolbarTop, b.width, kToolbar}, TypeStyle::Caption, Color::TextSecondary,
                    ui::TextAlign::Trailing);
}

} // namespace wl::app
