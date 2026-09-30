#include "app/pages/images/OperationStrip.h"

#include "ui/anim/Tween.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kIconX = 17.0f;
constexpr float kTextX = 40.0f;
constexpr float kRightInfo = 156.0f; // space right of the bar for the percentage + cancel

std::pair<Str, Str> labelsFor(EngineOperation::Kind kind) {
    switch (kind) {
    case EngineOperation::Kind::Preparing: return {Str::ImagesPreparing, Str::ImagesPreparingHint};
    case EngineOperation::Kind::Mounting: return {Str::ImagesMounting, Str::ImagesMountingHint};
    case EngineOperation::Kind::Unmounting: return {Str::ImagesUnmounting, Str::ImagesUnmountingHint};
    case EngineOperation::Kind::Exporting: return {Str::ImagesExporting, Str::ImagesExportingHint};
    case EngineOperation::Kind::Deleting: return {Str::ImagesDeleting, Str::ImagesWorkingHint};
    case EngineOperation::Kind::Cleaning: return {Str::ImagesCleaning, Str::ImagesWorkingHint};
    case EngineOperation::Kind::Reading: return {Str::ImagesReading, Str::ImagesReadingHint};
    case EngineOperation::Kind::Renaming: return {Str::ImagesRenaming, Str::ImagesWorkingHint};
    case EngineOperation::Kind::Verifying: return {Str::ImagesVerifying, Str::ImagesExportingHint};
    }
    return {Str::ImagesMounting, Str::ImagesWorkingHint};
}

// Reading: the page whose list is being read (EngineOperation::stage).
Str stageName(int stage) {
    constexpr Str kStages[]{Str::NavFeatures, Str::NavComponents, Str::NavServices};
    return kStages[std::clamp(stage, 0, static_cast<int>(std::size(kStages)) - 1)];
}
} // namespace

OperationStrip::OperationStrip(const Localization& strings, const AppState& state) : m_strings(strings), m_state(state) {
    m_cancel = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::CommonCancel));
    m_cancel->onInvoke = [this] {
        if (onCancel) {
            onCancel();
        }
    };
}

void OperationStrip::start() {
    m_now = ui::nowMs();
    animate();
}

bool OperationStrip::tick(double now) {
    m_now = now;
    invalidate();
    return visible() && m_state.operation().has_value();
}

void OperationStrip::layout() {
    const RectF b = bounds();
    const ui::SizeF size = m_cancel->measure({});
    m_cancel->setBounds({b.right() - 8 - size.width, b.y + 16, size.width, size.height});
}

std::wstring OperationStrip::eta(const EngineOperation& op, double now) const {
    const double elapsed = (now - op.startedMs) / 1000.0;
    if (op.fraction < 0.03 || elapsed < 2) {
        return L"…";
    }
    const int left = static_cast<int>(std::round(elapsed * (1.0 - op.fraction) / op.fraction));
    if (left >= 60) {
        return m_strings.format(Str::CommonEtaMinSec, {{L"m", std::to_wstring(left / 60)}, {L"s", std::to_wstring(left % 60)}});
    }
    return m_strings.format(Str::CommonEtaSec, {{L"s", std::to_wstring(left)}});
}

void OperationStrip::paint(ui::Canvas& canvas) {
    const auto& op = m_state.operation();
    if (!op) {
        return;
    }
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    // Spinner: 8 slices, 45° steps every 100 ms (progress-skeleton-empty.md: 800 ms per turn).
    const float angle = static_cast<float>(static_cast<int>(m_now / 100.0) % 8) * 45.0f;
    canvas.drawIcon(ui::icons::Icon::Spinner, {b.x + kIconX, b.y + 17}, Color::AccentBase, ui::IconVariant::Regular16, 0,
                    ui::reducedMotion() ? 0.0f : angle);

    const auto [titleKey, hintKey] = labelsFor(op->kind);
    const std::wstring pct = std::to_wstring(static_cast<int>(op->fraction * 100));
    const float textX = b.x + kTextX;
    const float textWidth = m_cancel->bounds().x - textX - 16;
    canvas.drawText(m_strings.format(titleKey, {{L"edition", op->edition}, {L"source", op->edition}}),
                    {textX, b.y + 17, textWidth, 16}, TypeStyle::BodyStrong, Color::TextPrimary);
    canvas.drawText(m_strings.format(hintKey, {{L"path", op->path.wstring()},
                                               {L"pct", pct},
                                               {L"eta", eta(*op, m_now)},
                                               {L"stage", m_strings.get(stageName(op->stage))},
                                               {L"step", std::to_wstring(op->stage + 1)}}),
                    {textX, b.y + 33, textWidth, 16}, TypeStyle::Caption, Color::TextSecondary);
    const float barRight = b.right() - kRightInfo;
    canvas.progressBar({textX, b.y + 62, barRight - textX, 2}, static_cast<float>(op->fraction));
    canvas.drawText(pct + L"%", {barRight + 8, b.y + 55, b.right() - 8 - barRight - 8, 16}, TypeStyle::Mono,
                    Color::TextSecondary, ui::TextAlign::Trailing);
}

} // namespace wl::app
