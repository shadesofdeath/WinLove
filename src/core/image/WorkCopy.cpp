#include "core/image/WorkCopy.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/UdfImage.h"

#include <json.hpp>
#include <windows.h>

#include <format>
#include <map>

namespace wl::core {

namespace {

using Json = nlohmann::json;

struct FileStamp {
    std::uint64_t size = 0;
    std::int64_t time = 0;
    bool operator==(const FileStamp&) const = default;
};

std::optional<FileStamp> stampOf(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto time = std::filesystem::last_write_time(file, ec);
    if (ec) {
        return std::nullopt;
    }
    return FileStamp{size, static_cast<std::int64_t>(time.time_since_epoch().count())};
}

// The image files of a setup folder: what mounting, editing and rebuilding change.
std::map<std::wstring, FileStamp> imageFiles(const std::filesystem::path& folder) {
    std::map<std::wstring, FileStamp> files;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(folder / L"sources", ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const std::wstring ext = text::lower(it->path().extension().wstring());
        if (ext != L".wim" && ext != L".esd" && ext != L".swm") {
            continue;
        }
        if (const auto stamp = stampOf(it->path())) {
            files.emplace(text::lower(it->path().filename().wstring()), *stamp);
        }
    }
    return files;
}

Json stampsToJson(const std::map<std::wstring, FileStamp>& files) {
    Json out = Json::object();
    for (const auto& [name, stamp] : files) {
        out[utf8::fromWide(name)] = Json{{"size", stamp.size}, {"time", stamp.time}};
    }
    return out;
}

std::optional<std::map<std::wstring, FileStamp>> stampsFromJson(const Json& in) {
    if (!in.is_object()) {
        return std::nullopt;
    }
    std::map<std::wstring, FileStamp> files;
    for (const auto& [name, value] : in.items()) {
        if (!value.is_object() || !value.value("size", Json()).is_number_unsigned() ||
            !value.value("time", Json()).is_number_integer()) {
            return std::nullopt;
        }
        files.emplace(utf8::toWide(name), FileStamp{value["size"].get<std::uint64_t>(), value["time"].get<std::int64_t>()});
    }
    return files;
}

bool folderEmpty(const std::filesystem::path& folder) {
    std::error_code ec;
    return !std::filesystem::is_directory(folder, ec) || std::filesystem::is_empty(folder, ec);
}

void writeRecord(const std::filesystem::path& folder, const FileStamp& iso, bool complete) {
    Json doc{{"version", 1}, {"isoSize", iso.size}, {"isoTime", iso.time}, {"complete", complete}};
    if (complete) {
        doc["images"] = stampsToJson(imageFiles(folder));
    }
    if (auto written = writeFileAtomic(workCopyRecord(folder), doc.dump(1)); !written) {
        log::warn("udf", L"work copy record not written: " + describe(written.error()));
    }
}

} // namespace

const wchar_t* workCopyStateName(WorkCopyState state) noexcept {
    switch (state) {
    case WorkCopyState::Missing: return L"missing";
    case WorkCopyState::Partial: return L"partial";
    case WorkCopyState::Pristine: return L"as extracted";
    case WorkCopyState::Modified: return L"modified";
    case WorkCopyState::Unknown: return L"no record";
    case WorkCopyState::OtherSource: return L"another ISO's";
    }
    return L"?";
}

std::filesystem::path workCopyRecord(const std::filesystem::path& folder) {
    const std::filesystem::path normal = folder.lexically_normal();
    const std::filesystem::path named = normal.has_filename() ? normal : normal.parent_path();
    return named.parent_path() / (named.filename().wstring() + L".source.json");
}

WorkCopyState inspectWorkCopy(const std::filesystem::path& iso, const std::filesystem::path& folder) {
    if (folderEmpty(folder)) {
        return WorkCopyState::Missing;
    }
    const auto bytes = readFileBytes(workCopyRecord(folder));
    if (!bytes) {
        return WorkCopyState::Unknown;
    }
    const Json doc = Json::parse(*bytes, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.value("isoSize", Json()).is_number_unsigned() ||
        !doc.value("isoTime", Json()).is_number_integer()) {
        return WorkCopyState::Unknown;
    }
    const auto stamp = stampOf(iso);
    if (!stamp || doc["isoSize"].get<std::uint64_t>() != stamp->size || doc["isoTime"].get<std::int64_t>() != stamp->time) {
        return WorkCopyState::OtherSource;
    }
    if (!doc.value("complete", false)) {
        return WorkCopyState::Partial;
    }
    const auto recorded = stampsFromJson(doc.value("images", Json()));
    if (!recorded) {
        return WorkCopyState::Unknown;
    }
    return *recorded == imageFiles(folder) ? WorkCopyState::Pristine : WorkCopyState::Modified;
}

Result<void> extractWorkCopy(const std::filesystem::path& iso, const std::filesystem::path& folder, bool fresh,
                             const TaskContext& task) {
    const auto stamp = stampOf(iso);
    if (!stamp) {
        return fail(ErrorCode::NotFound, L"ISO file not found", iso.wstring());
    }
    auto image = UdfImage::open(iso);
    if (!image) {
        return std::unexpected(image.error());
    }
    std::error_code ec;
    if (fresh && !folderEmpty(folder)) {
        log::info("udf", L"work copy started over: " + folder.wstring());
        std::filesystem::remove_all(folder, ec);
        if (ec) {
            return fail(ErrorCode::IoError, L"could not empty the work folder (a file in use?)", folder.wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value())));
        }
    }
    std::filesystem::create_directories(folder, ec);
    std::filesystem::remove(workCopyRecord(folder), ec);
    writeRecord(folder, *stamp, /*complete=*/false);
    if (auto r = image->extractAll(folder, task, /*keepExisting=*/true); !r) {
        return r;
    }
    writeRecord(folder, *stamp, /*complete=*/true);
    return {};
}

} // namespace wl::core
