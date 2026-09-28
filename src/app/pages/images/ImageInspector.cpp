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

std::wstring formatDate(std::uint64_t filetime, Language language) {
    if (filetime == 0) {
        return L"—";
    }
    FILETIME utc{static_cast<DWORD>(filetime & 0xFFFFFFFF), static_cast<DWORD>(filetime >> 32)};
    FILETIME local{};
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&utc, &local);
    FileTimeToSystemTime(&local, &st);
    wchar_t buffer[64]{};
    GetDateFormatEx(localeName(language), DATE_SHORTDATE, &st, nullptr, buffer, 64, nullptr);
    return buffer;
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
        if (onDelete) {
            onDelete();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::CommonDetails));
}

void ImageInspector::set(const core::SourceInfo* source, const core::ImageInfo* image, bool mountedHere, bool canMount,
                         bool canDelete, std::wstring mountTooltip, std::wstring deleteTooltip) {
    m_source = source ? std::optional<core::SourceInfo>(*source) : std::nullopt;
    m_image = image ? std::optional<core::ImageInfo>(*image) : std::nullopt;
    m_mountedHere = mountedHere;
    m_primary->setText(m_strings.get(mountedHere ? Str::ImagesUnmount : Str::ImagesMount));
    m_primary->setEnabled(mountedHere || canMount);
    m_primary->setTooltip(std::move(mountTooltip));
    m_delete->setEnabled(canDelete);
    m_delete->setTooltip(std::move(deleteTooltip));
    m_primary->setVisible(m_image.has_value());
    m_delete->setVisible(m_image.has_value());
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
    canvas.drawText(image.name, {x + 24, y, width - 24, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
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
    row(Str::ImagesEdition, image.editionId.empty() ? L"—" : image.editionId);
    row(Str::ImagesBuild, core::releaseSummary(image.build, image.spBuild, core::architectureName(image.architecture)));
    row(Str::ImagesArch, core::architectureName(image.architecture));
    row(Str::ImagesLang, image.defaultLanguage.empty()
                             ? L"—"
                             : std::format(L"{} ({})", image.defaultLanguage, m_strings.get(Str::ImagesDefaultLang)));
    row(Str::ImagesCreated, formatDate(image.creationTime, m_language));
    row(Str::CommonSize, formatBytes(image.totalBytes, m_language), true);

    section(Str::ImagesCompression);
    const std::wstring container = m_source->installImage.ends_with(L".esd") ? L"ESD" : L"WIM";
    row(Str::CommonType, std::format(L"{} ({})", upper(core::compressionName(m_source->install.header.compression)), container));
    row(Str::ImagesSplit, m_strings.get(m_source->install.header.totalParts > 1 ? Str::CommonYes : Str::CommonNo));

    section(Str::ImagesActionsTitle);
    canvas.drawText(m_strings.get(Str::ImagesActionsHint), {x, y - 4, width, kLine}, TypeStyle::Caption, Color::TextTertiary);
}

} // namespace wl::app
