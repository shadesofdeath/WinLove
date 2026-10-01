#pragma once
// A Win32 HANDLE closed on scope exit (CreateFile, CreateEvent … — INVALID_HANDLE_VALUE and null
// both mean "none").
#include <windows.h>

#include <utility>

namespace wl::core {

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : m_handle(handle) {}
    UniqueHandle(UniqueHandle&& other) noexcept : m_handle(std::exchange(other.m_handle, INVALID_HANDLE_VALUE)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            close();
            m_handle = std::exchange(other.m_handle, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    ~UniqueHandle() { close(); }

    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr; }

private:
    void close() noexcept {
        if (*this) {
            CloseHandle(m_handle);
        }
        m_handle = INVALID_HANDLE_VALUE;
    }
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

} // namespace wl::core
