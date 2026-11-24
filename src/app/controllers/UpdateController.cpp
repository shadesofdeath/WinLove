#include "app/controllers/UpdateController.h"

#include "base/Log.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <format>

namespace wl::app {

bool updateCheckDue(const AppSettings& settings, std::int64_t now) noexcept {
    if (!settings.checkUpdates) {
        return false;
    }
    const std::int64_t last = settings.updateCheckedAt;
    return last <= 0 || last > now + 3600 || now - last >= kUpdateInterval;
}

bool updateNoticeDue(const core::ReleaseCheck& check, std::wstring_view notifiedTag) {
    return check.verdict == core::ReleaseVerdict::Available && check.latest && !check.latest->tag.empty() &&
           check.latest->tag != notifiedTag;
}

std::optional<AppState::AppUpdate::Install::Why> installBlocker(const AppState& state) {
    using Why = AppState::AppUpdate::Install::Why;
    if (state.mounted()) {
        return Why::Mounted;
    }
    const auto& apply = state.applyRun();
    const auto& iso = state.isoRun();
    if (state.operation() || (apply && apply->stage != AppState::ApplyRun::Stage::Done) || (iso && iso->running) ||
        state.windowsDownload() || state.updateFetch() || state.languageFetch() || state.storeFetch()) {
        return Why::Busy;
    }
    return std::nullopt;
}

std::wstring UpdateController::restartArguments(std::wstring_view fromVersion, bool elevated) {
    std::wstring args = L"--relaunched " + core::quoteArgument(L"--updated-from=" + std::wstring(fromVersion));
    if (!elevated) {
        args += L" --no-elevate"; // the new one starts as this one runs: no UAC prompt for an update
    }
    return args;
}

UpdateController::Installer UpdateController::systemInstaller(std::function<void()> close) {
    Installer in;
    wchar_t path[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    in.exe = std::filesystem::path(std::wstring(path, n));
    in.writable = [](const std::filesystem::path& folder) { return core::checkFolderWritable(folder); };
    in.download = [](const core::ReleaseAsset& asset, const std::filesystem::path& exe, const core::TaskContext& task,
                     const std::function<void(std::uint64_t, std::uint64_t)>& bytes) {
        return core::downloadUpdate(asset, exe, L"WinLove/" WL_VERSION_STRING, task, bytes);
    };
    in.verify = [](const std::filesystem::path& file, const core::UpdateExpectation& expect, const core::TaskContext& task) {
        return core::verifyUpdateFile(file, expect, task);
    };
    in.swap = [](const std::filesystem::path& exe, const std::filesystem::path& staged) { return core::swapInUpdate(exe, staged); };
    in.rollback = [](const std::filesystem::path& exe) { return core::rollbackUpdate(exe); };
    in.launch = [](const std::filesystem::path& exe, const std::wstring& arguments) -> Result<void> {
        // Started as this process runs (no "runas"), in the same working folder.
        std::wstring line = core::quoteArgument(exe.wstring()) + L" " + arguments;
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            const DWORD code = GetLastError();
            return std::unexpected(Error{ErrorCode::IoError, L"the new WinLove could not be started", exe.wstring(),
                                         static_cast<std::int32_t>(HRESULT_FROM_WIN32(code))});
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return {};
    };
    in.close = std::move(close);
    return in;
}

UpdateController::UpdateController(AppState& state, std::function<void(std::function<void()>)> postToUi, Fetch fetch, Clock clock)
    : m_state(state), m_post(std::move(postToUi)), m_fetch(std::move(fetch)), m_clock(clock ? std::move(clock) : Clock(&systemNow)),
      m_runner(std::make_unique<core::TaskRunner>()) {}

UpdateController::~UpdateController() {
    *m_alive = false;
}

std::int64_t UpdateController::systemNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

UpdateController::Fetch UpdateController::networkFetch() {
    return [](const core::CancelToken& cancel) -> Result<core::ReleaseCheck> {
        core::ReleaseCheckOptions options;
        options.userAgent = L"WinLove/" WL_VERSION_STRING;
        auto answer = core::checkLatestRelease(L"" WL_VERSION_LABEL, options, cancel);
        if (!answer) {
            return std::unexpected(answer.error());
        }
        return std::move(answer->check);
    };
}

bool UpdateController::checkIfDue() {
    if (m_running || !updateCheckDue(m_state.settings(), m_clock())) {
        return false;
    }
    start(/*manual=*/false);
    return true;
}

void UpdateController::checkNow() {
    if (!m_running) {
        start(/*manual=*/true);
    }
}

void UpdateController::drain() {
    m_runner->drain();
}

void UpdateController::setInstall(std::optional<AppState::AppUpdate::Install> install) {
    AppState::AppUpdate update = m_state.appUpdate();
    update.install = std::move(install);
    m_state.setAppUpdate(std::move(update));
}

void UpdateController::install() {
    using Install = AppState::AppUpdate::Install;
    using Why = Install::Why;
    const auto& u = m_state.appUpdate();
    if (!u.result || u.result->verdict != core::ReleaseVerdict::Available || !u.result->latest) {
        return;
    }
    if (u.install && u.install->stage != Install::Stage::Failed) {
        return; // one at a time; a ready swap waits for its restart
    }
    const core::Release release = *u.result->latest;
    Install st;
    st.tag = release.tag;
    st.name = release.title();
    auto refuse = [&](Why why, std::wstring detail = {}) {
        st.stage = Install::Stage::Failed;
        st.why = why;
        st.detail = std::move(detail);
        log::info("update", std::format(L"update not installed ({}): {}", static_cast<int>(why), st.detail));
        setInstall(std::move(st));
    };
    if (const auto why = installBlocker(m_state)) {
        refuse(*why);
        return;
    }
    const auto asset = core::pickUpdateAsset(release);
    if (!asset) {
        refuse(Why::NoAsset);
        return;
    }
    if (!m_installer.download || m_installer.exe.empty()) {
        refuse(Why::NotWritable, L"no installer"); // renders and tests without one
        return;
    }
    if (m_installer.writable) {
        if (auto ok = m_installer.writable(m_installer.exe.parent_path()); !ok) {
            refuse(Why::NotWritable, ok.error().message);
            return;
        }
    }
    st.stage = Install::Stage::Downloading;
    st.total = asset->size;
    const core::CancelToken cancel = st.cancel;
    setInstall(st);
    log::info("update", L"installing " + release.tag + L": " + asset->url);

    auto why = std::make_shared<std::atomic<int>>(static_cast<int>(Why::None));
    auto detail = std::make_shared<std::wstring>();
    auto lastPercent = std::make_shared<std::atomic<int>>(-1);
    const Installer installer = m_installer;
    const core::UpdateExpectation expect{asset->size, asset->sha256, release.version};
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    auto progress = [this, post, alive, lastPercent](std::uint64_t done, std::uint64_t total) {
        const int percent = total ? static_cast<int>(done * 100 / total) : -1;
        if (lastPercent->exchange(percent) == percent) {
            return;
        }
        post([this, alive, done, total] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            if (const auto& now = m_state.appUpdate().install; now && now->stage == AppState::AppUpdate::Install::Stage::Downloading) {
                auto next = *now;
                next.done = done;
                next.total = total ? total : next.total;
                setInstall(std::move(next));
            }
        });
    };
    auto verifying = [this, post, alive] {
        post([this, alive] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            if (const auto& now = m_state.appUpdate().install; now && now->stage == AppState::AppUpdate::Install::Stage::Downloading) {
                auto next = *now;
                next.stage = AppState::AppUpdate::Install::Stage::Verifying;
                next.done = next.total;
                setInstall(std::move(next));
            }
        });
    };
    m_runner->run<bool>(
        [installer, asset = *asset, expect, cancel, why, detail, progress, verifying](const core::TaskContext&) -> Result<bool> {
            const core::TaskContext task{cancel, {}};
            const core::UpdatePaths paths = core::updatePaths(installer.exe);
            auto dropDownload = [&] {
                std::error_code ignored;
                std::filesystem::remove(paths.staged, ignored);
                std::filesystem::remove(paths.staged.wstring() + L".part", ignored);
            };
            auto staged = installer.download(asset, installer.exe, task, progress);
            if (!staged) {
                if (staged.error().code == ErrorCode::Cancelled) {
                    *why = static_cast<int>(Why::Cancelled);
                    dropDownload(); // a stopped download leaves nothing next to the program
                } else {
                    *why = static_cast<int>(Why::Download); // the .part stays: the next attempt resumes it
                }
                *detail = staged.error().message;
                return std::unexpected(staged.error());
            }
            verifying();
            auto verdict = installer.verify ? installer.verify(*staged, expect, task) : Result<core::UpdateVerdict>{core::UpdateVerdict{}};
            if (!verdict || verdict->failure) {
                *why = static_cast<int>(!verdict && verdict.error().code == ErrorCode::Cancelled ? Why::Cancelled : Why::Verify);
                *detail = !verdict ? verdict.error().message
                                   : std::wstring(core::verifyFailureName(*verdict->failure)) + L": " + verdict->detail;
                dropDownload(); // not the release's file
                return fail(ErrorCode::ParseError, *detail, staged->wstring());
            }
            if (cancel.cancelled()) {
                *why = static_cast<int>(Why::Cancelled);
                dropDownload();
                return fail(ErrorCode::Cancelled, L"cancelled");
            }
            if (installer.swap) {
                if (auto swapped = installer.swap(installer.exe, *staged); !swapped) {
                    *why = static_cast<int>(Why::Swap);
                    *detail = swapped.error().message;
                    dropDownload();
                    return std::unexpected(swapped.error());
                }
            }
            return true;
        },
        [this, post, alive, why, detail](Result<bool> result) {
            post([this, alive, why, detail, ok = result.has_value()] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& now = m_state.appUpdate().install;
                if (!now) {
                    return;
                }
                auto next = *now;
                if (ok) {
                    next.stage = AppState::AppUpdate::Install::Stage::Ready;
                    next.done = next.total;
                    log::info("update", L"update " + next.tag + L" is in place: it starts with the next start of WinLove");
                } else {
                    next.stage = AppState::AppUpdate::Install::Stage::Failed;
                    next.why = static_cast<Why>(why->load());
                    next.detail = *detail;
                    log::warn("update", L"update " + next.tag + L" not installed: " + *detail);
                }
                setInstall(std::move(next));
            });
        });
}

void UpdateController::cancelInstall() {
    const auto& now = m_state.appUpdate().install;
    if (now && (now->stage == AppState::AppUpdate::Install::Stage::Downloading ||
                now->stage == AppState::AppUpdate::Install::Stage::Verifying)) {
        now->cancel.cancel();
    }
}

std::optional<AppState::AppUpdate::Install::Why> UpdateController::restart() {
    using Install = AppState::AppUpdate::Install;
    const auto& now = m_state.appUpdate().install;
    if (!now || now->stage != Install::Stage::Ready) {
        return std::nullopt;
    }
    if (const auto why = installBlocker(m_state)) {
        return why; // the new exe is in place; the restart waits until the work is done
    }
    const std::wstring args = restartArguments(L"" WL_VERSION_LABEL, core::isElevated());
    log::info("update", L"starting the new WinLove: " + m_installer.exe.wstring() + L" " + args);
    Result<void> launched = m_installer.launch ? m_installer.launch(m_installer.exe, args)
                                               : Result<void>{std::unexpected(Error{ErrorCode::Unsupported, L"no launcher"})};
    if (!launched) {
        // Back as it was before the swap: the old exe under its name, this window open.
        auto next = *now;
        next.stage = Install::Stage::Failed;
        next.why = Install::Why::Restart;
        next.detail = launched.error().message;
        if (m_installer.rollback) {
            if (auto back = m_installer.rollback(m_installer.exe); !back) {
                next.detail += L"; " + back.error().message;
            }
        }
        log::warn("update", L"the new WinLove did not start, the old one is back: " + next.detail);
        setInstall(std::move(next));
        return std::nullopt;
    }
    if (m_installer.close) {
        m_installer.close();
    }
    return std::nullopt;
}

void UpdateController::start(bool manual) {
    if (!m_fetch || !m_post) {
        return;
    }
    m_running = true;
    m_before = m_state.appUpdate();
    AppState::AppUpdate checking = m_before;
    checking.phase = AppState::AppUpdate::Phase::Checking;
    checking.manual = manual;
    m_state.setAppUpdate(std::move(checking));
    m_runner->run<core::ReleaseCheck>(
        [fetch = m_fetch](const core::TaskContext& task) { return fetch(task.cancel); },
        [this, post = m_post, alive = std::weak_ptr<bool>(m_alive), manual](Result<core::ReleaseCheck> result) {
            // On the network thread: only hand the answer to the UI thread.
            post([this, alive, manual, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                finish(std::move(result), manual);
            });
        });
}

void UpdateController::finish(Result<core::ReleaseCheck> result, bool manual) {
    m_running = false;
    const std::int64_t now = m_clock();
    AppState::AppUpdate update = m_state.appUpdate();
    update.manual = manual;
    update.at = now;
    if (!result) {
        if (result.error().code == ErrorCode::Cancelled) {
            m_state.setAppUpdate(m_before); // as it was: nothing was learnt
            return;
        }
        // Offline is a normal state: no toast; the About page says why. The next start tries again
        // (the time of the last answer is not touched).
        log::info("update", L"update check failed: " + describe(result.error()));
        update.phase = AppState::AppUpdate::Phase::Failed;
        update.error = result.error();
        m_state.setAppUpdate(std::move(update));
        return;
    }
    AppSettings settings = m_state.settings();
    settings.updateCheckedAt = now;
    settings.updateLatestTag = result->latest ? result->latest->tag : std::wstring();
    if (result->verdict == core::ReleaseVerdict::Available && result->latest) {
        if (manual) {
            // Asked for and seen on the About page: that is the notice; the startup check stays quiet for it.
            settings.updateNotifiedTag = result->latest->tag;
            update.notice = false;
        } else if (updateNoticeDue(*result, settings.updateNotifiedTag)) {
            settings.updateNotifiedTag = result->latest->tag; // saved now: never announced twice
            update.notice = true;
            update.toastShown = false;
        }
    } else {
        update.notice = false;
    }
    update.phase = AppState::AppUpdate::Phase::Done;
    update.result = std::move(*result);
    update.error = {};
    m_state.setSettings(std::move(settings));
    m_state.setAppUpdate(std::move(update));
}

} // namespace wl::app
