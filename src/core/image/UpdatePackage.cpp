#include "core/image/UpdatePackage.h"

#include <algorithm>
#include <cwctype>
#include <regex>

namespace wl::core {

namespace {
std::wstring lowered(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}
} // namespace

bool isUpdateFile(const std::filesystem::path& file) {
    const std::wstring ext = lowered(file.extension().wstring());
    return ext == L".msu" || ext == L".cab";
}

UpdateInfo analyzeUpdate(const std::filesystem::path& file) {
    UpdateInfo info;
    info.path = file;
    std::error_code ec;
    info.size = std::filesystem::file_size(file, ec);
    if (ec) {
        info.size = 0; // missing / unreadable: file_size returns (uintmax_t)-1
    }
    const std::wstring name = lowered(file.filename().wstring());

    static const std::wregex kb(LR"(kb(\d{6,8}))");
    std::wsmatch m;
    if (std::regex_search(name, m, kb)) {
        info.kb = L"KB" + m[1].str();
    }
    if (name.starts_with(L"windows11.0-")) {
        info.targetWindows = 11;
    } else if (name.starts_with(L"windows10.0-")) {
        info.targetWindows = 10;
    }
    for (const wchar_t* arch : {L"arm64", L"x64", L"x86"}) {
        if (name.find(std::wstring(L"-") + arch) != std::wstring::npos) {
            info.architecture = arch;
            break;
        }
    }
    if (name.find(L"ssu") != std::wstring::npos) {
        info.kind = UpdateKind::Ssu;
    } else if (name.find(L"ndp") != std::wstring::npos || name.find(L"dotnet") != std::wstring::npos) {
        info.kind = UpdateKind::DotNet;
    } else if (info.targetWindows != 0 && !info.kb.empty()) {
        info.kind = UpdateKind::Lcu; // windows1x.0-kbNNNNNNN-<arch>: cumulative (msu, or the cab inside one)
    }
    return info;
}

const wchar_t* updateKindKey(UpdateKind kind) noexcept {
    switch (kind) {
    case UpdateKind::Ssu: return L"ssu";
    case UpdateKind::Lcu: return L"lcu";
    case UpdateKind::DotNet: return L"dotnet";
    case UpdateKind::Other: return L"other";
    }
    return L"other";
}

UpdateKind updateKindFromKey(std::wstring_view key) noexcept {
    if (key == L"ssu") {
        return UpdateKind::Ssu;
    }
    if (key == L"lcu") {
        return UpdateKind::Lcu;
    }
    if (key == L"dotnet") {
        return UpdateKind::DotNet;
    }
    return UpdateKind::Other;
}

std::vector<std::filesystem::path> scanUpdates(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             folder, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        if (it->is_regular_file(ec) && isUpdateFile(it->path())) {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    return files;
}

} // namespace wl::core
