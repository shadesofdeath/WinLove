#pragma once
// One engine thread (docs/ARCHITECTURE.md §2.5): DISM sessions are thread-affine and image
// operations must never run in parallel. Jobs run in FIFO order; `done` is called on the engine
// thread — the app marshals it to the UI thread (ui::Window::post).
#include "base/Utf8.h"
#include "core/tasks/Task.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace wl::core {

class TaskRunner {
public:
    TaskRunner();
    ~TaskRunner(); // cancels queued jobs' tokens, finishes the running job, joins
    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    // Queue `work`; returns the token that cancels it.
    template <class T>
    CancelToken run(std::function<Result<T>(const TaskContext&)> work, std::function<void(Result<T>)> done,
                    ProgressFn progress = {}) {
        TaskContext context{CancelToken{}, std::move(progress)};
        CancelToken token = context.cancel;
        post([work = std::move(work), done = std::move(done), context = std::move(context)] {
            // `done` must always run (the UI waits for it): an exception becomes an error result.
            Result<T> result = fail(ErrorCode::Unknown, L"engine job threw an exception");
            if (context.cancel.cancelled()) {
                result = fail(ErrorCode::Cancelled, L"cancelled before start");
            } else {
                try {
                    result = work(context);
                } catch (const std::exception& e) {
                    result = fail(ErrorCode::Unknown, L"engine job threw an exception", utf8::toWide(e.what()));
                } catch (...) {
                }
            }
            if (done) {
                done(std::move(result));
            }
        }, token);
        return token;
    }

    [[nodiscard]] bool onEngineThread() const noexcept;
    // Blocks until everything queued so far has run (tests, shutdown ordering).
    void drain();

private:
    void post(std::function<void()> job, CancelToken token);
    void loop();

    struct Job {
        std::function<void()> run;
        CancelToken token;
    };
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::deque<Job> m_jobs;
    bool m_busy = false;
    bool m_stopping = false;
    std::thread m_thread;
};

} // namespace wl::core
