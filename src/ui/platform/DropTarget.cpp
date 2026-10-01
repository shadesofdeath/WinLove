#include "ui/platform/DropTarget.h"

#include <shellapi.h>

namespace wl::ui {

namespace {

bool processElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                          elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

} // namespace

DropTarget* DropTarget::registerOn(Window& window, DropCallbacks callbacks) {
    auto* target = new DropTarget(window, std::move(callbacks));
    if (processElevated()) {
        // UIPI blocks OLE drag & drop from a non-elevated Explorer into an elevated window, so
        // fall back to WM_DROPFILES: no hover feedback, but the drop itself works.
        target->m_legacy = true;
        window.setFileDropHandler([target](std::vector<std::filesystem::path> files, PointF where) {
            const auto& cb = target->m_callbacks;
            if (cb.enter && cb.enter(files, where)) {
                if (cb.drop) {
                    cb.drop(files, where);
                }
            } else if (cb.leave) {
                cb.leave();
            }
        });
        return target;
    }
    if (FAILED(RegisterDragDrop(window.hwnd(), target))) {
        target->Release();
        return nullptr;
    }
    return target; // the window holds a reference via OLE; we keep ours until revoke()
}

void DropTarget::revoke() {
    if (m_legacy) {
        m_window.setFileDropHandler(nullptr);
    } else {
        RevokeDragDrop(m_window.hwnd());
    }
    Release();
}

HRESULT DropTarget::QueryInterface(REFIID riid, void** object) {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *object = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG DropTarget::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_refs));
}

ULONG DropTarget::Release() {
    const LONG refs = InterlockedDecrement(&m_refs);
    if (refs == 0) {
        delete this;
    }
    return static_cast<ULONG>(refs);
}

PointF DropTarget::toClient(POINTL point) const {
    POINT p{point.x, point.y};
    ScreenToClient(m_window.hwnd(), &p);
    return {static_cast<float>(p.x) / m_window.scale(), static_cast<float>(p.y) / m_window.scale()};
}

std::vector<std::filesystem::path> DropTarget::filesOf(IDataObject* data) {
    std::vector<std::filesystem::path> files;
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium))) {
        return files;
    }
    if (auto* drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(length, L'\0');
            DragQueryFileW(drop, i, path.data(), length + 1);
            files.emplace_back(std::move(path));
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return files;
}

HRESULT DropTarget::DragEnter(IDataObject* data, DWORD /*keys*/, POINTL point, DWORD* effect) {
    const auto files = filesOf(data);
    m_accepted = !files.empty() && m_callbacks.enter && m_callbacks.enter(files, toClient(point));
    *effect = m_accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

HRESULT DropTarget::DragOver(DWORD /*keys*/, POINTL point, DWORD* effect) {
    if (m_callbacks.over) {
        m_callbacks.over(toClient(point));
    }
    *effect = m_accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

HRESULT DropTarget::DragLeave() {
    if (m_callbacks.leave) {
        m_callbacks.leave();
    }
    return S_OK;
}

HRESULT DropTarget::Drop(IDataObject* data, DWORD /*keys*/, POINTL point, DWORD* effect) {
    auto files = filesOf(data);
    *effect = m_accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (m_callbacks.leave) {
        m_callbacks.leave(); // clear drag visuals first
    }
    if (m_accepted && m_callbacks.drop) {
        m_callbacks.drop(files, toClient(point));
    }
    return S_OK;
}

} // namespace wl::ui
