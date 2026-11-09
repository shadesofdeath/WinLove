#include "app/pages/images/ImageInspector.h"

#include "app/Format.h"
#include "core/image/WindowsRelease.h"

#include <windows.h>

#include <cwctype>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
// D-107 (LAYOUT.md "Sağ panel 340 — WIM bilgisi"): 24 padding; "Dizin 5 · install.wim" caption
// over the name (Title); key / value rows in Small with a 118 key column; the mount block at the
// foot under a line — the primary action full width (40), the second one under it.
constexpr float kPadding = ui::tokens::size::panelPad;
constexpr float kKeyWidth = 104.0f;
constexpr float kRow = 23.0f;   // Small 16 + 7 apart (D-108)
constexpr float kLine = 18.0f;
constexpr float kIconButton = 28.0f;
constexpr float kButton = ui::tokens::size::controlLarge; // 40
constexpr float kFootGap = 14.0f;

// SERVICINGDATA/IMAGESTATE → localized sysprep state; unknown values are shown as-is.
std::wstring imageStateText(const std::wstring& state, const Localization& strings) {
    struct Entry {
        const wchar_t* value;
        Str key;
    };
    static constexpr Entry kStates[] = {
        {L"IMAGE_STATE_COMPLETE", Str::ImagesStateComplete},
        {L"IMAGE_STATE_GENERALIZE_RESEAL_TO_OOBE", Str::ImagesStateGeneralizeOobe},
        {L"IMAGE_STATE_SPECIALIZE_RESEAL_TO_OOBE", Str::ImagesStateSpecializeOobe},
        {L"IMAGE_STATE_GENERALIZE_RESEAL_TO_AUDIT", Str::ImagesStateGeneralizeAudit},
        {L"IMAGE_STATE_SPECIALIZE_RESEAL_TO_AUDIT", Str::ImagesStateSpecializeAudit},
        {L"IMAGE_STATE_UNDEPLOYABLE", Str::ImagesStateUndeployable},
    };
    for (const auto& e : kStates) {
        if (state == e.value) {
            return strings.get(e.key);
        }
    }
    return state.empty() ? L"—" : state;
}

std::wstring orDash(const std::wstring& text) {
    return text.empty() ? L"—" : text;
}

std::wstring upper(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towupper(c));
    }
    return text;
}
} // namespace

ImageInspector::ImageInspector(const Localization& strings, Language language) : m_strings(strings), m_language(language) {
    m_primary = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::ImagesMount));
    m_primary->onInvoke = [this] {
        if (m_mountedHere ? onUnmount : onMount) {
            (m_mountedHere ? onUnmount : onMount)();
        }
    };
    m_primary->setHeight(kButton);
    m_delete = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::ImagesDeleteIndex));
    m_delete->setHeight(kButton);
    m_delete->onInvoke = [this] {
        // A mounted edition cannot be deleted; what it can be is upgraded.
        if (const auto& action = m_mountedHere ? onUpgrade : onDelete) {
            action();
        }
    };
    auto rename = ui::Button::iconOnly(ui::icons::Icon::Edit, strings.get(Str::ImagesRename));
    m_rename = rename.get();
    m_rename->onInvoke = [this] {
        if (onRename) {
            onRename();
        }
    };
    addChild(std::move(rename));
    auto explore = ui::Button::iconOnly(ui::icons::Icon::OpenFolder, strings.get(Str::ImagesExploreMountHint));
    m_explore = explore.get();
    m_explore->onInvoke = [this] {
        if (onExplore) {
            onExplore();
        }
    };
    addChild(std::move(explore));
    setAccessible(ui::AccessRole::Group, strings.get(Str::CommonDetails));
}

void ImageInspector::set(State state) {
    m_source = state.source ? std::optional<core::SourceInfo>(*state.source) : std::nullopt;
    m_image = state.image ? std::optional<core::ImageInfo>(*state.image) : std::nullopt;
    m_mountedHere = state.mountedHere;
    m_marked = state.marked;
    m_primary->setText(m_strings.get(state.mountedHere ? Str::ImagesUnmount : Str::ImagesMount));
    m_primary->setEnabled(state.mountedHere || state.canMount);
    m_primary->setTooltip(std::move(state.mountTooltip));
    m_upgradeQueued = std::move(state.upgradeQueued);
    if (state.mountedHere) {
        m_delete->setText(m_strings.get(Str::ImagesUpgrade));
        m_delete->setEnabled(state.canUpgrade);
        m_delete->setTooltip(std::move(state.upgradeTooltip));
    } else {
        m_delete->setText(state.marked > 1
                              ? m_strings.format(Str::ImagesDeleteMany, {{L"n", std::to_wstring(state.marked)}})
                              : m_strings.get(Str::ImagesDeleteIndex));
        m_delete->setEnabled(state.canDelete);
        m_delete->setTooltip(std::move(state.deleteTooltip));
    }
    m_rename->setEnabled(state.canRename);
    // Enabled: what the pencil does; disabled: why not.
    m_rename->setTooltip(state.canRename ? m_strings.get(Str::ImagesRename) : std::move(state.renameTooltip));
    m_primary->setVisible(m_image.has_value());
    m_delete->setVisible(m_image.has_value());
    m_rename->setVisible(m_image.has_value());
    m_explore->setVisible(m_image.has_value() && state.mountedHere);
    layout();
    invalidate();
}

void ImageInspector::layout() {
    const RectF b = bounds();
    // Disabled buttons still show their tooltip: the tooltip explains why (e.g. ESD).
    const float width = std::max(b.width - 2 * kPadding, 0.0f);
    const float del = b.bottom() - kPadding - kButton;
    m_delete->setBounds({b.x + kPadding, del, width, kButton});
    m_primary->setBounds({b.x + kPadding, del - 8 - kButton, width, kButton});
    m_rename->setBounds({b.right() - kPadding - kIconButton + 6, b.y + kPadding - 6, kIconButton, kIconButton});
    m_explore->setBounds({b.right() - kPadding - 2 * kIconButton + 4, b.y + kPadding - 6, kIconButton, kIconButton});
}

void ImageInspector::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    // D-114: a card like the others (rounded, no seam), not a strip glued to the window edge.
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgPanel);
    if (!m_image || !m_source) {
        return;
    }
    const auto& image = *m_image;
    const float x = b.x + kPadding;
    const float width = b.width - 2 * kPadding;
    float y = b.y + kPadding;
    // "Dizin 1 · install.wim", then the name; room for the pencil (and the folder while mounted).
    const float buttons = (m_mountedHere ? 2 * kIconButton : kIconButton) + 4;
    canvas.drawText(std::format(L"{} {} \u00b7 {}", m_strings.get(Str::ImagesIndex), image.index,
                                std::filesystem::path(m_source->installImage).filename().wstring()),
                    {x, y, width - buttons, 16}, TypeStyle::Caption, Color::TextTertiary);
    y += 18;
    canvas.drawText(image.name, {x, y, width, 22}, TypeStyle::Title, Color::TextPrimary);
    y += 30;
    const bool queued = m_mountedHere && !m_upgradeQueued.empty();
    if (queued || m_marked > 1) {
        canvas.drawText(queued ? m_upgradeQueued : m_strings.format(Str::ImagesSelectedHint, {{L"n", std::to_wstring(m_marked)}}),
                        {x, y, width, kLine}, TypeStyle::Small, queued ? Color::AccentText : Color::TextTertiary);
        y += kLine + 4;
    }
    // The foot (mount block) keeps its room: the facts are clipped above it.
    const float foot = m_primary->bounds().y - kFootGap;
    canvas.hairlineH(x, foot, width, Color::LineSubtle);
    canvas.pushClip({b.x, y, b.width, std::max(foot - 8 - y, 0.0f)});
    auto row = [&](Str key, const std::wstring& value, bool mono = false) {
        canvas.drawText(m_strings.get(key), {x, y, kKeyWidth - 8, kRow}, TypeStyle::Small, Color::TextTertiary);
        canvas.drawText(value, {x + kKeyWidth, y, width - kKeyWidth, kRow}, mono ? TypeStyle::Mono : TypeStyle::Small,
                        Color::TextPrimary);
        y += kRow;
    };
    auto divider = [&] {
        y += 9;
        canvas.hairlineH(x, y, width, Color::LineSubtle);
        y += 10;
    };
    auto section = [&](Str title) {
        divider();
        canvas.drawText(m_strings.get(title), {x, y, width, 18}, TypeStyle::Section, Color::TextTertiary);
        y += 24;
    };
    row(Str::ImagesEdition, orDash(image.editionId));
    row(Str::ImagesBuild, core::releaseSummary(image.build, image.spBuild, core::architectureName(image.architecture)));
    row(Str::ImagesBranch, orDash(image.branch), true);
    row(Str::ImagesArch, core::architectureName(image.architecture));
    std::wstring languages = image.defaultLanguage.empty()
                                 ? L"—"
                                 : std::format(L"{} ({})", image.defaultLanguage, m_strings.get(Str::ImagesDefaultLang));
    if (image.languages.size() > 1) {
        languages += std::format(L" +{}", image.languages.size() - 1);
    }
    row(Str::ImagesLang, languages);
    std::wstring installType = orDash(image.installationType);
    if (!image.productType.empty()) {
        installType += L" · " + image.productType;
    }
    row(Str::ImagesInstallType, installType);
    row(Str::ImagesImageState, imageStateText(image.imageState, m_strings));
    divider();
    row(Str::ImagesCreated, formatDate(image.creationTime, m_language));
    row(Str::ImagesModified, formatDate(image.modifiedTime, m_language));
    row(Str::CommonSize, formatBytes(image.totalBytes, m_language), true);
    row(Str::ImagesContents, m_strings.format(Str::ImagesFilesDirs, {{L"files", formatCount(image.fileCount, m_language)},
                                                                     {L"dirs", formatCount(image.directoryCount, m_language)}}));
    if (image.wimBoot) {
        row(Str::CommonType, L"WIMBoot");
    }

    section(Str::ImagesWimFile);
    const auto& header = m_source->install.header;
    const std::wstring container = m_source->installImage.ends_with(L".esd") ? L"ESD" : L"WIM";
    std::wstring compression = std::format(L"{} ({})", upper(core::compressionName(header.compression)), container);
    if (header.solid) {
        compression += L" · " + m_strings.get(Str::ImagesSolid);
    }
    row(Str::ImagesCompression, compression);
    row(Str::ImagesFileSize, formatBytes(m_source->installImageSize, m_language), true);
    row(Str::ImagesImageCount, std::to_wstring(m_source->install.images.size()));
    row(Str::ImagesSplit, header.totalParts > 1 ? std::format(L"{} / {}", header.partNumber, header.totalParts)
                                                : m_strings.get(Str::CommonNo));
    row(Str::ImagesBootIndex, header.bootIndex ? std::to_wstring(header.bootIndex) : m_strings.get(Str::ImagesNone));
    canvas.popClip();
}

} // namespace wl::app
