#pragma once
// COM for the calling thread. The engine threads have none, and a tool's main thread may not
// either; when the thread already has COM in another apartment mode, CoInitializeEx fails, nothing
// is owned and the calls still work. COM pointers: Microsoft::WRL::ComPtr.
#include <windows.h>

#include <objbase.h>

namespace wl::core {

class ComScope {
public:
    // Multithreaded by default; Shell windows (IShellWindows) want a single-threaded apartment.
    explicit ComScope(DWORD apartment = COINIT_MULTITHREADED) : m_owned(SUCCEEDED(CoInitializeEx(nullptr, apartment))) {}
    ~ComScope() {
        if (m_owned) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool m_owned;
};

} // namespace wl::core
