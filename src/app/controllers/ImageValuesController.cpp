#include "app/controllers/ImageValuesController.h"

#include "app/catalog/ImageSettingsCatalog.h"
#include "app/catalog/TweakCatalog.h"
#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/HostsFile.h"
#include "core/image/ImageFiles.h"
#include "core/image/ScheduledTasks.h"
#include "core/image/RegistryRead.h"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <format>

namespace wl::app {

using core::RegistryWrite;
using core::ops::OpKind;

ImageValueProbes ImageValueProbes::from(const TweakCatalog& tweaks, const ImageSettingsCatalog& settings) {
    ImageValueProbes probes;
    for (const auto& tweak : tweaks.tweaks()) {
        probes.writes.insert(probes.writes.end(), tweak.writes.begin(), tweak.writes.end());
    }
    for (const auto& setting : settings.settings()) {
        const bool text = setting.control == ImageSetting::Control::Text;
        for (std::size_t i = 0; i < setting.options.size(); ++i) {
            const auto& option = setting.options[i];
            if (text) {
                if (i == 1 && !option.writes.empty()) {
                    probes.texts.push_back(option.writes.front());
                }
                continue; // the listed data is a placeholder for what the user types
            }
            probes.writes.insert(probes.writes.end(), option.writes.begin(), option.writes.end());
            probes.files.insert(probes.files.end(), option.files.begin(), option.files.end());
        }
    }
    return probes;
}

Result<AppState::ImageValues> ImageValuesController::readImage(const std::filesystem::path& mountDir,
                                                               const ImageValueProbes& probes) {
    const auto started = std::chrono::steady_clock::now();
    core::OfflineRegistryReader reader(mountDir);
    AppState::ImageValues values{AppState::ImageValues::Status::Ready, mountDir, {}, {}, {}};
    std::size_t failed = 0;
    std::optional<Error> firstError;
    for (const auto& write : probes.writes) {
        auto holds = reader.holds(write);
        if (!holds) {
            if (holds.error().code != ErrorCode::Unsupported) {
                ++failed;
                if (!firstError) {
                    firstError = holds.error();
                }
            }
            continue;
        }
        if (*holds) {
            values.held.insert(AppState::imageValueKey(OpKind::SetRegistryValue, core::registryTarget(write),
                                                       core::formatRegValue(write)));
        }
    }
    if (firstError && failed == probes.writes.size()) {
        return std::unexpected(*firstError); // not one value readable: the hives are not
    }
    if (firstError) {
        log::warn("app", std::format(L"image values: {} of {} registry reads failed, first: {}", failed,
                                     probes.writes.size(), describe(*firstError)));
    }
    for (const auto& [path, content] : probes.files) {
        if (auto has = core::imageFileHas(mountDir, path, utf8::fromWide(content)); has && *has) {
            values.held.insert(AppState::imageValueKey(OpKind::WriteFile, path, content));
        }
    }
    for (const auto& write : probes.texts) {
        auto value = reader.value(write.key, write.name);
        if (!value || !*value || ((*value)->type != REG_SZ && (*value)->type != REG_EXPAND_SZ)) {
            continue;
        }
        const auto& data = (*value)->data;
        std::wstring text(data.size() / sizeof(wchar_t), L'\0');
        std::memcpy(text.data(), data.data(), text.size() * sizeof(wchar_t));
        while (!text.empty() && text.back() == L'\0') {
            text.pop_back();
        }
        if (!text.empty()) {
            values.texts.emplace(core::registryTarget(write), std::move(text));
        }
    }
    // D-048 / D-049: what an earlier run left in the image's tasks.cmd and hosts file.
    values.disabledTasks = core::readDisabledTasks(mountDir);
    for (const auto& task : values.disabledTasks) {
        values.held.insert(AppState::imageValueKey(OpKind::SetTaskState, task, L"disabled"));
    }
    values.hostsSections = core::readHostsSections(mountDir);
    for (const auto& [id, entries] : values.hostsSections) {
        values.held.insert(AppState::imageValueKey(OpKind::SetHosts, id, entries));
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    log::info("app", std::format(L"image values: {} of {} catalog writes / files already in the image, {} text value(s) ({} ms)",
                                 values.held.size(), probes.writes.size() + probes.files.size(), values.texts.size(),
                                 ms.count()));
    return values;
}

ImageValuesController::ImageValuesController(AppState& state, ImageValueProbes probes,
                                             std::function<void(std::function<void()>)> postToUi, Reader reader)
    : m_state(state), m_probes(std::move(probes)), m_post(std::move(postToUi)), m_reader(std::move(reader)) {
    // An Uygula run that ends with the image still mounted (stopped, commit failed) changed
    // the hives: read them again. A committed run unmounts, the next mount reads anyway.
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change != AppState::Change::Apply) {
            return;
        }
        const auto& run = m_state.applyRun();
        const bool running = run && run->stage != AppState::ApplyRun::Stage::Done;
        if (m_applying && !running && m_state.mounted()) {
            std::weak_ptr<bool> alive = m_alive;
            m_post([this, alive] {
                if (const auto a = alive.lock(); a && *a) {
                    load(/*force=*/true);
                }
            });
        }
        m_applying = running;
    });
}

ImageValuesController::~ImageValuesController() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

void ImageValuesController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.imageValues();
    if (!force && current && current->mountDir == mounted->mountDir &&
        current->status != AppState::ImageValues::Status::Failed) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setImageValues(AppState::ImageValues{AppState::ImageValues::Status::Loading, mountDir, {}, {}, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    // Engine thread: the hive files must not be open while DISM unmounts the same folder.
    m_state.engine().run<AppState::ImageValues>(
        [reader = m_reader, probes = m_probes, mountDir](const core::TaskContext&) { return reader(mountDir, probes); },
        [this, post, alive, mountDir](Result<AppState::ImageValues> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& mounted = m_state.mounted();
                if (!mounted || mounted->mountDir != mountDir) {
                    return;
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    AppState::ImageValues failed{AppState::ImageValues::Status::Failed, mountDir, {}, {}, result.error()};
                    m_state.setImageValues(std::move(failed));
                    return;
                }
                m_state.setImageValues(std::move(*result));
            });
        },
        {});
}

std::optional<RegistryWrite> revertWrite(const RegistryWrite& write) {
    RegistryWrite back = write;
    back.data.clear();
    switch (write.kind) {
    case RegistryWrite::Kind::Set: back.kind = RegistryWrite::Kind::DeleteValue; return back;
    case RegistryWrite::Kind::CreateKey:
        back.kind = RegistryWrite::Kind::DeleteKey;
        back.name.clear();
        return back;
    case RegistryWrite::Kind::DeleteValue:
    case RegistryWrite::Kind::DeleteKey: return std::nullopt;
    }
    return std::nullopt;
}

bool assertsSomething(const RegistryWrite& write) noexcept {
    return write.kind == RegistryWrite::Kind::Set || write.kind == RegistryWrite::Kind::CreateKey;
}

} // namespace wl::app
