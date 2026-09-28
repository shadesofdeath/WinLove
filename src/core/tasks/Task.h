#pragma once
// Cancellation and progress for long engine operations (mount, extract, apply).
// A CancelToken is cheap to copy; all copies share one state. DISM wants a Win32 event for
// cancellation, so the token owns one (created on first use).
#include "base/Result.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string_view>

namespace wl::core {

class CancelToken {
public:
    CancelToken();
    void cancel() const;
    [[nodiscard]] bool cancelled() const noexcept;
    // Manual-reset event signaled on cancel (DismMountImage & co. take it).
    [[nodiscard]] HANDLE event() const;
    // Convenience for loops: fail(ErrorCode::Cancelled) when cancelled.
    [[nodiscard]] Result<void> check(std::wstring_view context = {}) const;

private:
    struct State;
    std::shared_ptr<State> m_state;
};

// fraction in [0, 1] or negative for "indeterminate"; stage is a short human label.
using ProgressFn = std::function<void(double fraction, std::wstring_view stage)>;

struct TaskContext {
    CancelToken cancel;
    ProgressFn progress; // may be empty

    void report(double fraction, std::wstring_view stage = {}) const {
        if (progress) {
            progress(fraction, stage);
        }
    }
};

} // namespace wl::core
