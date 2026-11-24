#include "app/shell/ChromeText.h"

#include "app/Format.h"

namespace wl::app {

namespace {

std::wstring fileName(const std::filesystem::path& path) {
    const std::wstring name = path.filename().wstring();
    return name.empty() ? path.wstring() : name;
}

} // namespace

StatusBar::Model statusModel(const AppState& state, const Localization& strings, Language language) {
    StatusBar::Model model;
    if (const auto& mounted = state.mounted()) {
        model.mounted = true;
        model.image = mounted->edition.empty() ? fileName(mounted->imagePath) : mounted->edition;
        model.imageTip = strings.format(Str::StatusImageTip, {{L"path", mounted->mountDir.wstring()}});
    } else {
        model.image = strings.get(Str::StatusNoMount);
        model.imageTip = strings.get(Str::StatusNoMountTip);
    }
    const auto& changes = state.changes();
    if (const std::size_t n = changes.size(); n > 0) {
        model.changes = n == 1 ? strings.get(Str::StatusChangesOne)
                               : strings.format(Str::StatusChangesMany, {{L"n", formatCount(n, language)}});
        model.changesTip = strings.get(Str::StatusChangesTip);
        if (const std::int64_t delta = changes.estimatedSizeDelta(); delta != 0) {
            const auto bytes = static_cast<std::uint64_t>(delta < 0 ? -delta : delta);
            model.size = strings.format(delta < 0 ? Str::StatusSizeSaved : Str::StatusSizeAdded,
                                        {{L"size", formatBytes(bytes, language)}});
            model.sizeTip = strings.get(Str::StatusSizeTip);
        }
    }
    // D-140: a newer WinLove, until the About page has been seen. D-145: while it installs, its progress;
    // once it is in place, the restart (a click on the segment does it).
    const auto& update = state.appUpdate();
    using Stage = AppState::AppUpdate::Install::Stage;
    if (const auto& install = update.install; install && (install->stage == Stage::Downloading || install->stage == Stage::Verifying)) {
        const int pct = install->total ? static_cast<int>(install->done * 100 / install->total) : 0;
        model.update = strings.format(Str::AppUpdateSelfStatusDownloading, {{L"pct", std::to_wstring(std::clamp(pct, 0, 100))}});
        model.updateTip = strings.format(Str::AppUpdateSelfDownloadingTitle, {{L"name", install->name}});
    } else if (install && install->stage == Stage::Ready) {
        model.update = strings.get(Str::AppUpdateSelfStatusReady);
        model.updateTip = strings.get(Str::AppUpdateSelfStatusReadyTip);
    } else if (update.notice && update.result && update.result->latest && update.result->verdict == core::ReleaseVerdict::Available) {
        model.update = strings.format(Str::AppUpdateStatus, {{L"name", update.result->latest->title()}});
        model.updateTip = strings.get(Str::AppUpdateSelfMenuTip);
    }
    return model;
}

} // namespace wl::app
