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
constexpr float kPadding = 16.0f;
constexpr float kKeyWidth = 96.0f;
constexpr float kRow = 24.0f;
constexpr float kLine = 16.0f;

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
    m_delete = &add<ui::Button>(ui::ButtonKind::Subtle, strings.get(Str::ImagesDeleteIndex));
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
    const ui::SizeF primary = m_primary->measure({});
    const ui::SizeF del = m_delete->measure({});
    const float y = b.bottom() - 12 - primary.height;
    m_primary->setBounds({b.x + kPadding, y, primary.width, primary.height});
    m_delete->setBounds({b.x + kPadding + primary.width + 4, y, del.width, del.height});
    m_rename->setBounds({b.right() - kPadding - kRow, b.y + kPadding - 4, kRow, kRow});
    m_explore->setBounds({b.right() - kPadding - 2 * kRow - 2, b.y + kPadding - 4, kRow, kRow});
}

void ImageInspector::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineV(b.x, b.y, b.height, Color::LineSubtle);
    if (!m_image || !m_source) {
        return;
    }
    const auto& image = *m_image;
    const float x = b.x + kPadding;
    const float width = b.width - 2 * kPadding;
    float y = b.y + kPadding;

    canvas.drawIcon(ui::icons::Icon::LayersEditions, {x, y + 4}, Color::TextSecondary);
    // Room for the pencil, and for the folder while this edition is mounted.
    const float buttons = (m_mountedHere ? 2 * kRow + 2 : kRow) + 4;
    canvas.drawText(image.name, {x + 24, y, width - 24 - buttons, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
    canvas.drawText(std::format(L"{} · {} {}", std::filesystem::path(m_source->installImage).filename().wstring(),
                                m_strings.get(Str::ImagesIndex), image.index),
                    {x + 24, y + kLine, width - 24, kLine}, TypeStyle::Caption, Color::TextTertiary);

    y = b.y + 69; // design: first key row at y+69 (panel top 33 → text 102)
    auto row = [&](Str key, const std::wstring& value, bool mono = false) {
        canvas.drawText(m_strings.get(key), {x, y, kKeyWidth, kRow}, TypeStyle::Caption, Color::TextSecondary);
        canvas.drawText(value, {x + kKeyWidth, y, width - kKeyWidth, kRow}, mono ? TypeStyle::Mono : TypeStyle::Caption,
                        Color::TextPrimary);
        y += kRow;
    };
    auto section = [&](Str title) {
        y += 12;
        canvas.hairlineH(b.x + 1, y, b.width - 1, Color::LineSubtle);
        y += 8;
        canvas.drawText(m_strings.get(title), {x, y, width, kRow}, TypeStyle::Section, Color::TextSecondary);
        y += kRow;
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

    section(Str::ImagesActionsTitle);
    const bool queued = m_mountedHere && !m_upgradeQueued.empty();
    canvas.drawText(queued         ? m_upgradeQueued
                    : m_marked > 1 ? m_strings.format(Str::ImagesSelectedHint, {{L"n", std::to_wstring(m_marked)}})
                                   : m_strings.get(Str::ImagesActionsHint),
                    {x, y - 4, width, kLine}, TypeStyle::Caption, queued ? Color::AccentBase : Color::TextTertiary);
}

} // namespace wl::app
