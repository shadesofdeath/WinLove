#include "app/pages/apply/ImpactCard.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kHeader = 38.0f;
constexpr float kRow = 32.0f;
constexpr float kPadX = 16.0f;
constexpr float kNameWidth = 230.0f;
constexpr float kBottom = 8.0f;
} // namespace

ImpactCard::ImpactCard(const Localization& strings, std::vector<Row> rows) : m_strings(strings), m_rows(std::move(rows)) {
    const std::size_t listed = std::min<std::size_t>(m_rows.size(), kMaxRows);
    for (std::size_t i = 0; i < listed; ++i) {
        auto& button = add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::ApplyImpactKeep), ui::icons::Icon::ShieldCheck);
        button.setHeight(26.0f);
        const std::wstring guard = m_rows[i].guard;
        button.onInvoke = [this, guard] {
            if (onKeep) {
                onKeep(guard);
            }
        };
        button.setAccessible(ui::AccessRole::Button, strings.get(Str::ApplyImpactKeep) + L": " + m_rows[i].name);
        m_keep.push_back(&button);
    }
    if (m_rows.size() > listed) {
        m_more = &add<ui::Button>(ui::ButtonKind::Ghost,
                                  strings.format(Str::ApplyImpactMore, {{L"n", std::to_wstring(m_rows.size() - listed)}}));
        m_more->setHeight(26.0f);
        m_more->onInvoke = [this] {
            if (onMore) {
                onMore();
            }
        };
    }
    setAccessible(ui::AccessRole::Group, strings.get(Str::ApplyImpactTitle));
}

float ImpactCard::preferredHeight() const {
    const float rows = static_cast<float>(m_keep.size()) + (m_more ? 1.0f : 0.0f);
    return kHeader + rows * kRow + (rows > 0 ? kBottom : 0.0f);
}

void ImpactCard::layout() {
    const RectF b = bounds();
    float y = b.y + kHeader;
    for (auto* button : m_keep) {
        const ui::SizeF size = button->measure({});
        button->setBounds({b.right() - kPadX - size.width, y + (kRow - size.height) / 2, size.width, size.height});
        y += kRow;
    }
    if (m_more) {
        const ui::SizeF size = m_more->measure({});
        m_more->setBounds({b.x + kPadX - 8, y + (kRow - size.height) / 2, size.width, size.height});
    }
}

void ImpactCard::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const bool clean = m_rows.empty();
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgRaised);
    canvas.fillRoundRect({b.x + 1, b.y + 8, 3, b.height - 16}, 1.5f, clean ? Color::StatusSuccess : Color::StatusWarning);

    // Header: what this is, or that nothing known breaks.
    const float iconY = b.y + std::round((kHeader - ui::tokens::size::icon) / 2);
    canvas.drawIcon(clean ? ui::icons::Icon::SuccessCircle : ui::icons::Icon::WarningTriangle, {b.x + kPadX, iconY},
                    clean ? Color::StatusSuccess : Color::StatusWarning);
    const float textX = b.x + kPadX + ui::tokens::size::icon + 10;
    const std::wstring title =
        clean ? m_strings.get(Str::ApplyImpactNone) : m_strings.format(Str::ApplyImpactTitleN, {{L"n", std::to_wstring(m_rows.size())}});
    const float titleW = std::ceil(canvas.text().measure(title, TypeStyle::BodyStrong));
    canvas.drawText(title, {textX, b.y, std::min(titleW + 1, b.right() - textX - kPadX), kHeader}, TypeStyle::BodyStrong,
                    Color::TextPrimary);
    if (!clean) {
        const float hintX = textX + titleW + 16;
        canvas.drawText(m_strings.get(Str::ApplyImpactHint), {hintX, b.y, std::max(b.right() - kPadX - hintX, 0.0f), kHeader},
                        TypeStyle::Caption, Color::TextTertiary, ui::TextAlign::Trailing);
    }

    // One row per feature: its name (an app's with a tag), the steps that break it, the button.
    float y = b.y + kHeader;
    for (std::size_t i = 0; i < m_keep.size(); ++i) {
        const Row& row = m_rows[i];
        canvas.hairlineH(b.x + kPadX, y, b.width - 2 * kPadX, Color::LineSubtle);
        float x = b.x + kPadX;
        canvas.fillEllipse({x + 3, y + kRow / 2}, 3.0f, Color::StatusWarning);
        x += 14;
        const float nameW = std::min(std::ceil(canvas.text().measure(row.name, TypeStyle::BodyStrong)) + 1, kNameWidth);
        canvas.drawText(row.name, {x, y, nameW, kRow}, TypeStyle::BodyStrong, Color::TextPrimary);
        if (row.app) {
            const std::wstring tag = m_strings.get(Str::ApplyImpactAppTag);
            const float tagW = std::ceil(canvas.text().measure(tag, TypeStyle::Small)) + 12;
            const RectF tagRect{x + nameW + 8, y + (kRow - 18) / 2, tagW, 18};
            canvas.fillRoundRect(tagRect, 9.0f, Color::BgPressed);
            canvas.drawText(tag, tagRect, TypeStyle::Small, Color::TextSecondary, ui::TextAlign::Center);
        }
        const float itemsX = b.x + kPadX + 14 + kNameWidth + 70;
        const float itemsRight = m_keep[i]->bounds().x - 16;
        canvas.drawText(row.items, {itemsX, y, std::max(itemsRight - itemsX, 0.0f), kRow}, TypeStyle::Caption, Color::TextSecondary);
        y += kRow;
    }
    if (m_more) {
        canvas.hairlineH(b.x + kPadX, y, b.width - 2 * kPadX, Color::LineSubtle);
    }
}

} // namespace wl::app
