#include "core/tasks/TaskRunner.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <objbase.h>

#include <exception>

namespace wl::core {

TaskRunner::TaskRunner() : m_thread([this] { loop(); }) {}

TaskRunner::~TaskRunner() {
    {
        std::scoped_lock lock(m_mutex);
        m_stopping = true;
        for (auto& job : m_jobs) {
            job.token.cancel(); // queued jobs report Cancelled instead of running
        }
        // The running job as well: without it a hash of a 6 GB ISO or a driver export went on to
        // its end with the window gone, the process (and its one-instance mutex) with it.
        if (m_running) {
            m_running->cancel();
        }
    }
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool TaskRunner::onEngineThread() const noexcept {
    return std::this_thread::get_id() == m_thread.get_id();
}

void TaskRunner::post(std::function<void()> job, CancelToken token) {
    {
        std::scoped_lock lock(m_mutex);
        if (m_stopping) {
            token.cancel(); // queued by a job while the runner goes away: reports Cancelled like the rest
        }
        m_jobs.push_back({std::move(job), std::move(token)});
    }
    m_wake.notify_one();
}

void TaskRunner::drain() {
    if (onEngineThread()) {
        return; // a job waiting for its own end would never wake (audit ENG-16)
    }
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_jobs.empty() && !m_busy; });
}

void TaskRunner::loop() {
    // COM on the engine thread: DISM, WIC and virtdisk helpers expect it.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
            if (m_jobs.empty()) {
                break; // stopping and nothing left
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            m_running = job.token;
            m_busy = true;
        }
        try {
            job.run();
        } catch (const std::exception& e) {
            // Engine code reports through Result; an exception here is a bug — log, keep the thread alive.
            log::error("task", L"unhandled exception in engine job: " + utf8::toWide(e.what()));
        } catch (...) {
            // `done` may throw anything; the thread must outlive it (audit ENG-16).
            log::error("task", L"unhandled exception in engine job");
        }
        {
            std::scoped_lock lock(m_mutex);
            m_running.reset();
            m_busy = false;
        }
        m_idle.notify_all();
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

} // namespace wl::core
