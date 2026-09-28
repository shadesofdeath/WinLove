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
        m_jobs.push_back({std::move(job), std::move(token)});
    }
    m_wake.notify_one();
}

void TaskRunner::drain() {
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
            m_busy = true;
        }
        try {
            job.run();
        } catch (const std::exception& e) {
            // Engine code reports through Result; an exception here is a bug — log, keep the thread alive.
            log::error("task", L"unhandled exception in engine job: " + utf8::toWide(e.what()));
        }
        {
            std::scoped_lock lock(m_mutex);
            m_busy = false;
        }
        m_idle.notify_all();
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

} // namespace wl::core
