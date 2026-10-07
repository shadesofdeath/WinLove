#include "app/pages/programs/ProgramInspector.h"

#include "base/Text.h"

#include <cmath>
#include <cwctype>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kPadding = 16.0f;
constexpr float kKeyWidth = 80.0f;
constexpr float kRow = 24.0f;
constexpr float kIcon = 32.0f;
constexpr int kDescriptionLines = 9;

std::wstring joined(const std::vector<std::wstring>& parts, std::wstring_view separator) {
    std::wstring out;
    for (const auto& part : parts) {
        out += (out.empty() ? L"" : std::wstring(separator)) + part;
    }
    return out;
}

// "Google.Chrome" → "Google": the publisher until the details say better.
std::wstring publisherOf(const std::wstring& id) {
    return id.substr(0, id.find(L'.'));
}
} // namespace

ProgramInspector::ProgramInspector(const Localization& strings, Language language) : m_strings(strings), m_language(language) {
    m_toggle = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::ProgramsAdd), ui::icons::Icon::Add);
    m_toggle->onInvoke = [this] {
        if (onToggle) {
            onToggle();
        }
    };
    m_homepage = &add<ui::Button>(ui::ButtonKind::Subtle, strings.get(Str::ProgramsHomepage), ui::icons::Icon::ExternalLink);
    m_homepage->onInvoke = [this] {
        if (onOpenUrl && m_details && !m_details->homepage.empty()) {
            onOpenUrl(m_details->homepage);
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::CommonDetails));
}

void ProgramInspector::paintLetter(ui::Canvas& canvas, RectF box, std::wstring_view name, bool large) {
    canvas.fillRoundRect(box, ui::tokens::radius::r2, Color::AccentSubtle);
    const wchar_t first = name.empty() ? L'?' : static_cast<wchar_t>(std::towupper(name.front()));
    canvas.drawText(std::wstring(1, first), box, large ? TypeStyle::Title : TypeStyle::Caption, Color::AccentBase,
                    ui::TextAlign::Center);
}

void ProgramInspector::set(std::optional<core::WingetPackage> package, const core::WingetDetails* details,
                           std::filesystem::path icon, bool picked, bool failed) {
    m_package = std::move(package);
    m_details = details ? std::optional<core::WingetDetails>(*details) : std::nullopt;
    m_icon = std::move(icon);
    m_picked = picked;
    m_failed = failed;
    m_toggle->setText(m_strings.get(picked ? Str::ProgramsRemove : Str::ProgramsAdd));
    m_toggle->setVisible(m_package.has_value());
    m_homepage->setVisible(m_details && text::istartsWith(m_details->homepage, L"https://"));
    layout();
    invalidate();
}

void ProgramInspector::layout() {
    const RectF b = bounds();
    const ui::SizeF toggle = m_toggle->measure({});
    const float y = b.bottom() - 12 - toggle.height;
    m_toggle->setBounds({b.x + kPadding, y, toggle.width, toggle.height});
    const ui::SizeF link = m_homepage->measure({});
    m_homepage->setBounds({b.x + kPadding + toggle.width + 8, y, link.width, link.height});
}

void ProgramInspector::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineV(b.x, b.y, b.height, Color::LineSubtle);
    if (!m_package) {
        return;
    }
    const auto& package = *m_package;
    const core::WingetDetails* d = m_details ? &*m_details : nullptr;
    const float x = b.x + kPadding;
    const float width = b.width - 2 * kPadding;
    float y = b.y + kPadding;

    const RectF iconBox{x, y, kIcon, kIcon};
    if (m_icon.empty() || !canvas.drawFileIcon(m_icon.wstring(), 0, iconBox)) {
        paintLetter(canvas, iconBox, package.name, /*large=*/true);
    }
    const float tx = x + kIcon + 10;
    const std::wstring& name = d && !d->name.empty() ? d->name : package.name;
    const std::wstring publisher = d && !d->publisher.empty() ? d->publisher : publisherOf(package.id);
    canvas.drawText(name, {tx, y, b.right() - kPadding - tx, 16}, TypeStyle::BodyStrong, Color::TextPrimary);
    canvas.drawText(publisher, {tx, y + 16, b.right() - kPadding - tx, 16}, TypeStyle::Caption, Color::TextSecondary);
    y += kIcon + 8;
    canvas.drawText(package.id, {x, y, width, 16}, TypeStyle::Mono, Color::TextTertiary);
    y += 24;

    auto section = [&](Str title) {
        canvas.hairlineH(b.x + 1, y, b.width - 1, Color::LineSubtle);
        y += 8;
        canvas.drawText(m_strings.get(title), {x, y, width, kRow}, TypeStyle::Section, Color::TextSecondary);
        y += kRow;
    };
    if (!d) {
        canvas.drawText(m_strings.get(m_failed ? Str::ProgramsDetailsFailed : Str::ProgramsDetailsLoading), {x, y, width, kRow},
                        TypeStyle::Caption, m_failed ? Color::StatusError : Color::TextTertiary);
        y += kRow;
    } else {
        const std::wstring& about = !d->shortDescription.empty() ? d->shortDescription : d->description;
        if (!about.empty()) {
            section(Str::ProgramsAbout);
            const float line = 16.0f;
            const float h = std::min(std::ceil(canvas.text().measureWrapped(about, TypeStyle::Caption, width)),
                                     line * kDescriptionLines);
            canvas.pushClip({x, y, width, h});
            canvas.drawTextWrapped(about, {x, y, width, h + line}, TypeStyle::Caption, Color::TextSecondary);
            canvas.popClip();
            y += h + 12;
        }
    }
    section(Str::CommonDetails);
    auto row = [&](Str key, const std::wstring& value, TypeStyle style = TypeStyle::Caption) {
        if (value.empty()) {
            return;
        }
        canvas.drawText(m_strings.get(key), {x, y, kKeyWidth, kRow}, TypeStyle::Caption, Color::TextSecondary);
        canvas.drawText(value, {x + kKeyWidth, y, width - kKeyWidth, kRow}, style, Color::TextPrimary);
        y += kRow;
    };
    row(Str::ProgramsVersion, d && !d->version.empty() ? d->version : package.version, TypeStyle::Mono);
    if (d) {
        row(Str::ProgramsLicense, d->license);
        std::vector<std::wstring> scopes;
        for (const auto& scope : d->scopes) {
            scopes.push_back(text::iequals(scope, L"user") ? m_strings.get(Str::ProgramsScopeUser)
                                                           : m_strings.get(Str::ProgramsScopeMachine));
        }
        std::wstring installer = joined(d->installerTypes, L", ");
        if (!scopes.empty()) {
            installer += L" · " + joined(scopes, L", ");
        }
        row(Str::ProgramsInstaller, installer);
        row(Str::ProgramsArchitecture, joined(d->architectures, L", "));
        if (!d->tags.empty()) {
            y += 4;
            section(Str::ProgramsTags);
            const std::wstring tags = joined(d->tags, L" · ");
            const float bottom = m_toggle->bounds().y - 12;
            const float h = std::max(0.0f, std::min(std::ceil(canvas.text().measureWrapped(tags, TypeStyle::Caption, width)),
                                                     bottom - y));
            canvas.pushClip({x, y, width, h});
            canvas.drawTextWrapped(tags, {x, y, width, h + 16}, TypeStyle::Caption, Color::TextTertiary);
            canvas.popClip();
        }
    }
}

} // namespace wl::app
