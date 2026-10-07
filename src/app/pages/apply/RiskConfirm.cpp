#include "app/pages/apply/RiskConfirm.h"

#include "app/Format.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/pages/ApplyPage.h"
#include "base/Text.h"

#include <algorithm>
#include <map>
#include <set>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kCheckGap = 8.0f;
constexpr float kSizeWidth = 80.0f;
constexpr float kValueWidth = 160.0f; // a setting's option ("Kapalı", "Yalnız güvenlik")
} // namespace

bool RiskConfirm::removes(core::ops::OpKind kind) noexcept {
    using core::ops::OpKind;
    switch (kind) {
    case OpKind::RemovePackage:
    case OpKind::RemoveCapability:
    case OpKind::RemoveComponent:
    case OpKind::RemoveDriver:
    case OpKind::RemoveAppx:
    case OpKind::CleanupImage:
    case OpKind::ShrinkStore: return true;
    default: return false;
    }
}

std::vector<RiskConfirm::Item> RiskConfirm::itemsFor(const AppState& state, const ImageSettingsCatalog& settings,
                                                     const Localization& strings, Language language,
                                                     std::span<const core::ops::Operation> risky) {
    // Queue slot -> the setting of Ayarlar / Tweaks that holds it with the option it is queued at.
    auto slot = [](core::ops::OpKind kind, const std::wstring& target) {
        return std::to_wstring(static_cast<int>(kind)) + L'|' + text::lower(target);
    };
    std::map<std::wstring, std::pair<const ImageSetting*, int>> owners;
    for (const auto& setting : settings.settings()) {
        const int option = ImageSettingsController::optionIn(state.changes(), setting);
        if (option == setting.defaultOption) {
            continue;
        }
        const std::wstring typed = ImageSettingsController::valueIn(state.changes(), setting);
        for (const auto& op : ImageSettingsController::operationsFor(setting, option, typed)) {
            owners.emplace(slot(op.kind, op.target), std::pair{&setting, option});
        }
    }
    std::vector<Item> items;
    std::set<const ImageSetting*> listed;
    for (const auto& op : risky) {
        if (removes(op.kind)) {
            items.push_back({ApplyPage::displayName(state, op),
                             op.sizeDelta < 0 ? formatBytes(static_cast<std::uint64_t>(-op.sizeDelta), language) : std::wstring(),
                             true});
            continue;
        }
        if (const auto owner = owners.find(slot(op.kind, op.target)); owner != owners.end()) {
            const auto [setting, option] = owner->second;
            if (listed.insert(setting).second) {
                const std::wstring value =
                    setting->control == ImageSetting::Control::Toggle ? strings.get(option == 1 ? Str::PresetsOn : Str::PresetsOff)
                    : ImageSettingsController::takesValue(*setting)
                        ? ImageSettingsController::valueIn(state.changes(), *setting)
                        : setting->options[static_cast<std::size_t>(option)].label.get(language);
                items.push_back({setting->label.get(language), value, false});
            }
            continue;
        }
        items.push_back({ApplyPage::displayName(state, op), std::wstring(), false});
    }
    return items;
}

RiskConfirm::RiskConfirm(std::vector<Item> items, std::wstring ack) : m_items(std::move(items)) {
    m_check = &add<ui::CheckField>(std::move(ack), false);
    m_check->onChange = [this](bool on) {
        if (onAck) {
            onAck(on);
        }
    };
    m_scroll = &add<ui::ScrollBar>();
    m_scroll->onScroll = [this](float offset) { scrollTo(offset); };
}

ui::RectF RiskConfirm::listRect() const {
    const RectF b = bounds();
    return {b.x, b.y, b.width, std::max(b.height - kCheckGap - kRow, 0.0f)};
}

void RiskConfirm::layout() {
    const RectF b = bounds();
    const RectF list = listRect();
    const float content = kRow * static_cast<float>(m_items.size());
    m_offset = std::clamp(m_offset, 0.0f, std::max(content - list.height, 0.0f));
    m_check->setBounds({b.x, list.bottom() + kCheckGap, b.width, kRow});
    m_scroll->setBounds({list.right() - ui::ScrollBar::kWidth, list.y, ui::ScrollBar::kWidth, list.height});
    m_scroll->setRange(content, list.height);
    m_scroll->setOffset(m_offset);
    m_scroll->setVisible(m_scroll->needed());
}

void RiskConfirm::scrollTo(float offset) {
    const float content = kRow * static_cast<float>(m_items.size());
    offset = std::clamp(offset, 0.0f, std::max(content - listRect().height, 0.0f));
    if (offset != m_offset) {
        m_offset = offset;
        m_scroll->setOffset(m_offset);
        invalidate();
    }
}

bool RiskConfirm::onWheel(ui::PointF /*p*/, float lines) {
    if (!m_scroll->needed()) {
        return false;
    }
    scrollTo(m_offset - lines * kRow);
    return true;
}

void RiskConfirm::paint(ui::Canvas& canvas) {
    const RectF list = listRect();
    // Leave the overlay bar its lane so it never covers the sizes.
    const float right = list.right() - (m_scroll->needed() ? ui::ScrollBar::kWidth : 0.0f);
    canvas.pushClip(list);
    const auto first = static_cast<std::size_t>(m_offset / kRow);
    float y = list.y + kRow * static_cast<float>(first) - m_offset;
    for (std::size_t i = first; i < m_items.size() && y < list.bottom(); ++i, y += kRow) {
        const auto& item = m_items[i];
        canvas.hairlineH(list.x, y, list.width, Color::LineSubtle);
        canvas.drawIcon(ui::icons::Icon::ShieldWarning, {list.x, y + 4}, item.removal ? Color::StatusError : Color::StatusWarning);
        const float x = list.x + ui::tokens::size::icon + 8;
        const float detail = item.removal ? kSizeWidth : kValueWidth;
        canvas.drawText(item.name, {x, y, right - x - detail, kRow}, TypeStyle::Body, Color::TextPrimary);
        canvas.drawText(item.size, {right - detail, y, detail, kRow}, item.removal ? TypeStyle::Mono : TypeStyle::Body,
                        Color::TextSecondary, ui::TextAlign::Trailing);
    }
    canvas.popClip();
    canvas.hairlineH(list.x, std::min(y, list.bottom()), list.width, Color::LineSubtle);
}

} // namespace wl::app
