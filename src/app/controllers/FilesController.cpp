#include "app/controllers/FilesController.h"

#include "base/Log.h"
#include "core/image/ImageFiles.h"

#include <cwctype>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Risk;

const std::vector<FilesController::Place>& FilesController::places() {
    static const std::vector<Place> kPlaces = {
        {L"Tools", Str::FilesPlaceTools},
        {L"Users\\Public\\Desktop", Str::FilesPlacePublicDesktop},
        {L"Users\\Default\\Desktop", Str::FilesPlaceDefaultDesktop},
        {L"Program Files", Str::FilesPlaceProgramFiles},
        {L"Users\\Public\\Documents", Str::FilesPlacePublicDocuments},
        {L"Windows\\Web\\Wallpaper\\WinLove", Str::FilesPlaceWallpaper},
        {L"", Str::FilesPlaceRoot},
    };
    return kPlaces;
}

std::wstring FilesController::imageFolderFrom(std::wstring_view typed) {
    std::wstring t(typed);
    for (auto& c : t) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    while (!t.empty() && (t.front() == L' ')) {
        t.erase(t.begin());
    }
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\\')) {
        t.pop_back();
    }
    if (t.size() >= 2 && t[1] == L':') {
        if (std::towupper(t[0]) != L'C') {
            return L"?"; // another drive: not in the image
        }
        t.erase(0, 2);
    }
    while (!t.empty() && t.front() == L'\\') {
        t.erase(t.begin());
    }
    return t;
}

std::wstring FilesController::displayPath(std::wstring_view target) {
    return L"C:\\" + std::wstring(target);
}

Result<Operation> FilesController::operationFor(const std::filesystem::path& source, std::wstring_view folder) {
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        return fail(ErrorCode::NotFound, L"the file or folder is not there", source.wstring());
    }
    const std::wstring name = source.filename().wstring();
    if (name.empty()) {
        return fail(ErrorCode::InvalidArgument, L"a drive cannot be copied as it is; pick a folder", source.wstring());
    }
    const std::wstring target = folder.empty() ? name : std::wstring(folder) + L"\\" + name;
    if (auto ok = core::validateTreeTarget(target); !ok) {
        return std::unexpected(ok.error());
    }
    Operation op{OpKind::CopyTree, target, source.wstring()};
    op.risk = core::treeTargetRisky(target) ? Risk::High : Risk::Low;
    op.sizeDelta = static_cast<std::int64_t>(core::treeSize(source));
    return op;
}

std::pair<int, std::vector<Error>> FilesController::add(const std::vector<std::filesystem::path>& sources,
                                                        std::wstring_view folder) {
    std::vector<Operation> ops;
    std::vector<Error> errors;
    for (const auto& source : sources) {
        auto op = operationFor(source, folder);
        if (!op) {
            log::warn("app", describe(op.error()));
            errors.push_back(op.error());
            continue;
        }
        ops.push_back(std::move(*op));
    }
    const int n = static_cast<int>(ops.size());
    m_state.queueMany(std::move(ops));
    return {n, std::move(errors)};
}

std::vector<Operation> FilesController::queued() const {
    std::vector<Operation> ops;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::CopyTree) {
            ops.push_back(op);
        }
    }
    return ops;
}

int FilesController::count() const {
    return static_cast<int>(m_state.changes().count(OpKind::CopyTree));
}

} // namespace wl::app
