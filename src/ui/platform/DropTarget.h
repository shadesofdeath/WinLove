#pragma once
// Window-wide file drop (interaction.md "Drag & drop"): OLE IDropTarget on the main HWND.
// The owner decides per drag whether the files are acceptable (copy cursor) or not (no-drop).
// Requires OleInitialize on the UI thread (main.cpp).
#include "ui/platform/Window.h"

#include <oleidl.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace wl::ui {

struct DropCallbacks {
    std::function<bool(const std::vector<std::filesystem::path>& files, PointF where)> enter; // true = accept
    std::function<void(PointF where)> over;
    std::function<void()> leave;
    std::function<void(const std::vector<std::filesystem::path>& files, PointF where)> drop;
};

class DropTarget final : public IDropTarget {
public:
    // Registers on window.hwnd(); revoked in revoke() (call before the window is destroyed).
    static DropTarget* registerOn(Window& window, DropCallbacks callbacks);
    void revoke();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) override;

private:
    DropTarget(Window& window, DropCallbacks callbacks) : m_window(window), m_callbacks(std::move(callbacks)) {}
    [[nodiscard]] PointF toClient(POINTL point) const;
    static std::vector<std::filesystem::path> filesOf(IDataObject* data);

    Window& m_window;
    DropCallbacks m_callbacks;
    std::vector<std::filesystem::path> m_files;
    bool m_accepted = false;
    LONG m_refs = 1;
};

} // namespace wl::ui
