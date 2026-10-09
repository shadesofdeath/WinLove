#include "core/uup/UupDownload.h"

#include "base/Log.h"
#include "base/Text.h"
#include "core/net/Http.h"
#include "core/system/Hash.h"
#include "core/updates/UupLanguages.h" // trustedUupUrl

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <optional>
#include <thread>

namespace wl::core::uup {

std::filesystem::path localPath(const std::filesystem::path& folder, std::wstring_view name) {
    std::filesystem::path out = folder;
    std::wstring part;
    bool flat = false;
    std::vector<std::wstring> parts;
    for (const wchar_t c : name) {
        if (c == L'/' || c == L'\\') {
            parts.push_back(std::move(part));
            part.clear();
        } else {
            part += c;
        }
    }
    parts.push_back(std::move(part));
    for (const auto& p : parts) {
        if (p.empty() || p == L"." || p == L".." || p.find(L':') != std::wstring::npos) {
            flat = true;
        }
    }
    if (flat) {
        std::wstring name2;
        for (const wchar_t c : name) {
            name2 += (c == L'/' || c == L'\\' || c == L':') ? L'_' : c;
        }
        return folder / name2;
    }
    for (const auto& p : parts) {
        out /= p;
    }
    return out;
}

namespace {

// The file at `path` is the listed one: its hash (or, with none published, its size).
Result<bool> matches(const std::filesystem::path& path, const File& file, const CancelToken& cancel) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size != file.size) {
        return false;
    }
    const TaskContext quiet{cancel, {}};
    if (!file.sha256.empty()) {
        auto h = sha256File(path, quiet);
        if (!h) {
            return std::unexpected(h.error());
        }
        return *h == file.sha256;
    }
    if (!file.sha1.empty()) {
        auto h = sha1File(path, quiet);
        if (!h) {
            return std::unexpected(h.error());
        }
        return *h == file.sha1;
    }
    log::warn("uup", L"no hash published, size only: " + file.name);
    return true;
}

bool expired(const Error& e) {
    return e.message.find(L"HTTP 403") != std::wstring::npos || e.message.find(L"HTTP 404") != std::wstring::npos ||
           e.message.find(L"HTTP 410") != std::wstring::npos;
}

} // namespace

Result<void> downloadFiles(std::vector<File> files, const std::filesystem::path& folder,
                           const std::function<Result<std::vector<File>>()>& refresh, const TaskContext& task,
                           int connections) {
    std::uint64_t total = 0;
    for (const auto& f : files) {
        total += f.size;
    }
    log::info("uup", std::format(L"download {} file(s), {} bytes -> {}", files.size(), total, folder.wstring()));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    std::mutex lock;          // files[].url, generation, error, report
    int generation = 0;       // bumped by each refresh of the links
    std::optional<Error> error;
    std::atomic<std::uint64_t> finished{0};
    const int workers = std::clamp(connections, 1, 8);
    std::vector<std::atomic<std::uint64_t>> inFlight(static_cast<std::size_t>(workers));
    std::atomic<std::size_t> next{0};
    auto lastReport = std::chrono::steady_clock::now();
    const CancelToken stopAll; // a failed file, or the caller's cancel, stops every connection
    auto stopped = [&] { return task.cancel.cancelled() || stopAll.cancelled(); };

    auto report = [&](std::wstring_view stage) {
        const std::lock_guard guard(lock);
        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport < std::chrono::milliseconds(200)) {
            return;
        }
        lastReport = now;
        std::uint64_t done = finished.load();
        for (const auto& b : inFlight) {
            done += b.load();
        }
        task.report(total ? static_cast<double>(done) / static_cast<double>(total) : 0.0, stage);
    };
    auto setError = [&](Error e) {
        const std::lock_guard guard(lock);
        if (!error) {
            error = std::move(e);
        }
        stopAll.cancel();
    };

    auto work = [&](std::size_t slot) {
        const CancelToken cancel = stopAll;
        for (std::size_t i = next++; i < files.size() && !stopped(); i = next++) {
            const File& file = files[i];
            const auto target = localPath(folder, file.name);
            std::error_code dirEc;
            std::filesystem::create_directories(target.parent_path(), dirEc);
            if (std::filesystem::exists(target, dirEc)) {
                auto ok = matches(target, file, cancel);
                if (ok && *ok) {
                    finished += file.size;
                    report(L"verify");
                    continue;
                }
                std::filesystem::remove(target, dirEc);
            }
            int linkGeneration = 0;
            std::wstring url;
            int attempts = 0;
            for (;;) {
                if (stopped()) {
                    return;
                }
                {
                    const std::lock_guard guard(lock);
                    url = files[i].url;
                    linkGeneration = generation;
                }
                const TaskContext one{stopAll, {}};
                // Only Microsoft's servers (the list comes from a third party; the bytes must not).
                if (!url.empty() && !trustedUupUrl(url)) {
                    setError(Error{ErrorCode::AccessDenied, L"the link is not a Microsoft download server", url});
                    return;
                }
                auto got = url.empty() ? Result<std::uint64_t>(fail(ErrorCode::IoError, L"HTTP 403 (no link)", file.name))
                                       : httpDownload(url, target, file.size, one,
                                                      [&](std::uint64_t have, std::uint64_t) {
                                                          if (task.cancel.cancelled()) {
                                                              stopAll.cancel();
                                                          }
                                                          inFlight[slot] = have;
                                                          report(L"download");
                                                      },
                                                      [](std::wstring_view final) { return trustedUupUrl(final); });
                if (!got && !stopped() && expired(got.error()) && refresh && attempts < 3) {
                    ++attempts;
                    const std::lock_guard guard(lock);
                    if (generation == linkGeneration) { // nobody refreshed since this link was read
                        log::info("uup", L"links expired: asking for new ones");
                        auto fresh = refresh();
                        if (!fresh) {
                            error = fresh.error();
                            stopAll.cancel();
                            return;
                        }
                        for (auto& f : files) {
                            for (const auto& n : *fresh) {
                                if (n.name == f.name) {
                                    f.url = n.url;
                                }
                            }
                        }
                        ++generation;
                    }
                    continue;
                }
                if (!got) {
                    if (!stopped() && attempts < 2 && !expired(got.error())) {
                        ++attempts; // a dropped connection: the .part continues
                        log::warn("uup", std::format(L"{}: {}; again", file.name, got.error().message));
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        continue;
                    }
                    if (!stopped()) {
                        setError(got.error());
                    }
                    return;
                }
                inFlight[slot] = 0;
                auto ok = matches(target, file, cancel);
                if (!ok) {
                    setError(ok.error());
                    return;
                }
                if (*ok) {
                    finished += file.size;
                    report(L"download");
                    break;
                }
                std::filesystem::remove(target, dirEc);
                if (++attempts > 2) {
                    setError(Error{ErrorCode::IoError, L"the downloaded file does not match its published hash", file.name});
                    return;
                }
                log::warn("uup", L"hash mismatch, downloading again: " + file.name);
            }
        }
    };
    {
        std::vector<std::jthread> threads;
        for (int w = 0; w < workers; ++w) {
            threads.emplace_back(work, static_cast<std::size_t>(w));
        }
    }
    if (task.cancel.cancelled()) {
        return fail(ErrorCode::Cancelled, L"download cancelled", folder.wstring());
    }
    if (error) {
        return std::unexpected(*error);
    }
    task.report(1.0, L"download");
    return {};
}

} // namespace wl::core::uup
