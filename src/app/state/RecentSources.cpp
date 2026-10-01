#include "app/state/RecentSources.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Path.h"
#include "base/Utf8.h"
#include "core/image/WindowsRelease.h"

#include <json.hpp>

#include <windows.h>

#include <algorithm>

namespace wl::app {

namespace {

bool samePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec)) {
        return true;
    }
    return _wcsicmp(a.lexically_normal().c_str(), b.lexically_normal().c_str()) == 0;
}

std::int64_t toUnix(std::chrono::system_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

} // namespace

std::filesystem::path RecentSources::defaultFile() {
    return log::defaultDirectory().parent_path() / L"recent.json";
}

void RecentSources::load() {
    m_entries.clear();
    const auto bytes = readFileBytes(m_file);
    if (!bytes) {
        return;
    }
    const auto doc = nlohmann::json::parse(*bytes, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        log::warn("app", L"recent.json is corrupt; starting with an empty list");
        return;
    }
    const auto sources = doc.find("sources");
    if (sources == doc.end() || !sources->is_array()) {
        return;
    }
    for (const auto& e : *sources) {
        // value() throws on wrongly typed fields: a bad entry is skipped, never a startup crash.
        try {
            if (!e.is_object()) {
                continue;
            }
            RecentSource entry;
            entry.path = nativePath(utf8::toWide(e.value("path", "")));
            entry.format = utf8::toWide(e.value("format", ""));
            entry.summary = utf8::toWide(e.value("summary", ""));
            entry.size = e.value("size", std::uint64_t{0});
            entry.lastOpened =
                std::chrono::system_clock::time_point(std::chrono::seconds(e.value("lastOpened", std::int64_t{0})));
            if (!entry.path.empty() && m_entries.size() < kCapacity) {
                m_entries.push_back(std::move(entry));
            }
        } catch (const nlohmann::json::exception&) {
            log::warn("app", L"recent.json: skipped a malformed entry");
        }
    }
}

Result<void> RecentSources::save() const {
    nlohmann::json sources = nlohmann::json::array();
    for (const auto& e : m_entries) {
        sources.push_back({{"path", utf8::fromWide(e.path.wstring())}, {"format", utf8::fromWide(e.format)},
                           {"summary", utf8::fromWide(e.summary)}, {"size", e.size}, {"lastOpened", toUnix(e.lastOpened)}});
    }
    std::error_code ec;
    std::filesystem::create_directories(m_file.parent_path(), ec);
    // Replaced atomically: a crash never leaves a half-written file.
    return writeFileAtomic(m_file, nlohmann::json{{"version", 1}, {"sources", sources}}.dump(2));
}

void RecentSources::touch(const core::SourceInfo& source, std::chrono::system_clock::time_point when) {
    RecentSource entry;
    entry.path = source.path;
    entry.format = core::formatName(source.format);
    if (!source.install.images.empty()) {
        const auto& first = source.install.images.front();
        entry.summary = core::releaseSummary(first.build, first.spBuild, core::architectureName(first.architecture));
    }
    std::error_code ec;
    entry.size = source.format == core::ImageFormat::Folder ? source.installImageSize
                                                             : std::filesystem::file_size(source.path, ec);
    entry.lastOpened = when;
    std::erase_if(m_entries, [&](const RecentSource& e) { return samePath(e.path, source.path); });
    m_entries.insert(m_entries.begin(), std::move(entry));
    if (m_entries.size() > kCapacity) {
        m_entries.resize(kCapacity);
    }
    if (auto saved = save(); !saved) {
        log::warn("app", describe(saved.error()));
    }
}

void RecentSources::remove(const std::filesystem::path& path) {
    std::erase_if(m_entries, [&](const RecentSource& e) { return samePath(e.path, path); });
    if (auto saved = save(); !saved) {
        log::warn("app", describe(saved.error()));
    }
}

} // namespace wl::app
