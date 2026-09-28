#include "core/tasks/Task.h"

#include <mutex>

namespace wl::core {

struct CancelToken::State {
    std::atomic<bool> cancelled{false};
    mutable std::once_flag eventOnce;
    mutable HANDLE event = nullptr;
    ~State() {
        if (event) {
            CloseHandle(event);
        }
    }
};

CancelToken::CancelToken() : m_state(std::make_shared<State>()) {}

void CancelToken::cancel() const {
    m_state->cancelled = true;
    SetEvent(event());
}

bool CancelToken::cancelled() const noexcept {
    return m_state->cancelled.load();
}

HANDLE CancelToken::event() const {
    std::call_once(m_state->eventOnce, [this] {
        m_state->event = CreateEventW(nullptr, TRUE, m_state->cancelled.load() ? TRUE : FALSE, nullptr);
    });
    return m_state->event;
}

Result<void> CancelToken::check(std::wstring_view context) const {
    if (cancelled()) {
        return fail(ErrorCode::Cancelled, L"operation cancelled", std::wstring(context));
    }
    return {};
}

} // namespace wl::core
