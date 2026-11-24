#pragma once
// The "UI thread" of a unit test: what a controller hands to postToUi runs on the test's own thread,
// as the window's message loop runs it in the program (ui::Window::post) — never on the engine or
// reader thread that finished the job.
//
// The tests used to pass `[](std::function<void()> fn) { fn(); }`: the job's result then ran on the
// engine thread, changing AppState and laying out pages while the test's thread did the same — and
// a controller the test had already destroyed could still be in such a callback (past its liveness
// check). Release wl_tests.exe died of it about one run in ten (0xC0000374, heap corruption; ASan:
// heap-use-after-free in FeaturesPage::refresh on the engine thread).
//
//   test::UiThread ui;
//   FeatureController controller{state, ui.post()};
//   state.setMounted(...);
//   ui.settle(state.engine()); // the read finishes, its result lands here, and what that starts too
//
// Mode::Immediate (the default): a post made on the test's thread runs at once (App's headless post
// does the same); one from any other thread waits for run() / settle(). Mode::Queued: every post
// waits, as on the window's message loop. Posts that nobody runs are dropped with the last copy of
// post(): it keeps the queue alive, so a job that ends after the test's locals are gone (a runner
// reports its cancelled jobs when AppState goes) posts safely.
#include "core/tasks/TaskRunner.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace wl::test {

class UiThread {
public:
    using Post = std::function<void(std::function<void()>)>;
    enum class Mode {
        Immediate, // the test thread's own posts run at once (App's headless post)
        Queued,    // every post waits for run(), as on the window's message loop
    };

    explicit UiThread(Mode mode = Mode::Immediate) : m_queue(std::make_shared<Queue>()) {
        m_queue->mode = mode;
    }

    // The function for postToUi.
    [[nodiscard]] Post post() const {
        return [queue = m_queue](std::function<void()> fn) {
            if (queue->mode == Mode::Immediate && std::this_thread::get_id() == queue->owner) {
                fn();
                return;
            }
            std::scoped_lock lock(queue->mutex);
            queue->items.push_back(std::move(fn));
        };
    }

    // On the test's thread: runs what was posted until now, oldest first (one turn of the message
    // loop: what those post waits for the next run). Returns how many ran.
    std::size_t run() {
        std::deque<std::function<void()>> now;
        {
            std::scoped_lock lock(m_queue->mutex);
            now.swap(m_queue->items);
        }
        for (auto& fn : now) {
            fn();
        }
        return now.size();
    }

    [[nodiscard]] std::size_t pending() const {
        std::scoped_lock lock(m_queue->mutex);
        return m_queue->items.size();
    }

    // Until nothing more comes: every runner's jobs finish, then what they posted runs here (which
    // may queue the next job: a list read starts the probes that need it).
    template <class... Runners>
    void settle(Runners&... runners) {
        do {
            (runners.drain(), ...);
        } while (run() > 0);
    }

private:
    struct Queue {
        std::mutex mutex;
        std::deque<std::function<void()>> items;
        std::thread::id owner = std::this_thread::get_id();
        Mode mode = Mode::Immediate;
    };
    std::shared_ptr<Queue> m_queue;
};

} // namespace wl::test
