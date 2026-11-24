#pragma once
// D-140: "is there a newer WinLove?" — notify only. A check is one GET of GitHub's release list
// (core/net/ReleaseCheck: User-Agent "WinLove/<version>", nothing else about the machine or the user)
// on a thread of its own: never the UI thread, never behind a DISM job, never delaying startup.
//   - at start (App, a few seconds after the window is up): when Ayarlar › "Güncellemeleri denetle" is
//     on and the last answer is a day old; quiet — a failure is only what the About page says
//   - "Şimdi denetle" on the About page: always
// A newer release found by the startup check is announced once per tag (one toast with "Göster" and
// a status bar segment until the About page is seen); the tag is saved at once, so the same release
// is never announced again. Nothing is downloaded or installed.
// Only App makes one, in a windowed run: --render and the unit tests never reach the network (the
// tests give a fetch function of their own).
// D-145: "Yükle" — the newer release's WinLove.exe downloaded next to this one, checked (size, GitHub's
// SHA-256, x64 GUI program, its version) and swapped in (core/net/SelfUpdate), on the controller's own
// thread; then "Yeniden başlat" starts it ("--updated-from=<this version>") and closes this window.
// Refused while an image is mounted or work runs (a restart would cut it), and when the program's
// folder cannot be written to (the release page instead). Nothing runs elevated for it.
#include "app/state/AppState.h"
#include "core/net/ReleaseCheck.h"
#include "core/net/SelfUpdate.h"
#include "core/tasks/TaskRunner.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace wl::app {

inline constexpr std::int64_t kUpdateInterval = 24 * 3600; // at most one startup check a day

// The startup check is due: the setting is on and the last answer is at least a day old — or there
// was none, or it is "from the future" (the clock was set back).
[[nodiscard]] bool updateCheckDue(const AppSettings& settings, std::int64_t now) noexcept;
// The notice for this answer: a newer release, and not the one already announced.
[[nodiscard]] bool updateNoticeDue(const core::ReleaseCheck& check, std::wstring_view notifiedTag);
// D-145: why "Yükle" / "Yeniden başlat" must wait (nullopt: they need not): an image is mounted
// (Mounted), or an Apply, an ISO, an engine job or a download runs (Busy).
[[nodiscard]] std::optional<AppState::AppUpdate::Install::Why> installBlocker(const AppState& state);

class UpdateController {
public:
    using Fetch = std::function<Result<core::ReleaseCheck>(const core::CancelToken&)>;
    using Clock = std::function<std::int64_t()>; // seconds since 1970 (UTC)

    UpdateController(AppState& state, std::function<void(std::function<void()>)> postToUi, Fetch fetch, Clock clock = {});
    ~UpdateController();
    UpdateController(const UpdateController&) = delete;
    UpdateController& operator=(const UpdateController&) = delete;

    // At start: checks when updateCheckDue(); true when a check began.
    bool checkIfDue();
    // "Şimdi denetle".
    void checkNow();
    [[nodiscard]] bool running() const noexcept { return m_running; }
    // Tests: waits for the network thread (its answer is then posted, not yet applied).
    void drain();

    // GitHub's release list for this build (WL_VERSION_LABEL; User-Agent "WinLove/" WL_VERSION_STRING).
    [[nodiscard]] static Fetch networkFetch();
    [[nodiscard]] static std::int64_t systemNow();

    // D-145: the self-update's steps. The default works on this process's exe with core/net/SelfUpdate;
    // the tests give their own (no network, no file of the build touched).
    struct Installer {
        std::filesystem::path exe;
        std::function<Result<void>(const std::filesystem::path& folder)> writable;
        std::function<Result<std::filesystem::path>(const core::ReleaseAsset&, const std::filesystem::path& exe, const core::TaskContext&,
                                                    const std::function<void(std::uint64_t, std::uint64_t)>&)>
            download;
        std::function<Result<core::UpdateVerdict>(const std::filesystem::path& file, const core::UpdateExpectation&, const core::TaskContext&)>
            verify;
        std::function<Result<void>(const std::filesystem::path& exe, const std::filesystem::path& staged)> swap;
        std::function<Result<void>(const std::filesystem::path& exe)> rollback;
        std::function<Result<void>(const std::filesystem::path& exe, const std::wstring& arguments)> launch;
        std::function<void()> close; // the window closes once the new process runs
    };
    [[nodiscard]] static Installer systemInstaller(std::function<void()> close);
    void setInstaller(Installer installer) { m_installer = std::move(installer); }
    // "Yükle": the newest release (an Available check) — refused with the reason in the state when it
    // cannot run. "İptal" stops the download / check (the partial file goes). "Yeniden başlat": the
    // swapped-in exe starts; when it cannot, the old exe is put back. Returns why it waits, if it does.
    void install();
    void cancelInstall();
    std::optional<AppState::AppUpdate::Install::Why> restart();
    // The command line of the new instance ("--relaunched --updated-from=…", "--no-elevate" when this
    // one is not elevated: an update never asks for elevation).
    [[nodiscard]] static std::wstring restartArguments(std::wstring_view fromVersion, bool elevated);

private:
    void start(bool manual);
    void finish(Result<core::ReleaseCheck> result, bool manual);
    void setInstall(std::optional<AppState::AppUpdate::Install> install);
    Installer m_installer;

    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    Fetch m_fetch;
    Clock m_clock;
    bool m_running = false;
    AppState::AppUpdate m_before; // what the state said before the running check (a cancel goes back to it)
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_runner; // last: stopped and joined first
};

} // namespace wl::app
