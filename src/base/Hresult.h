#pragma once
// Turns a failed HRESULT into an early `return wl::fail(...)`, recording the failing expression.
//   WL_TRY_HR(device->CreateBitmap(...), ErrorCode::RenderFailure, L"creating target bitmap");
#include "base/Result.h"

#define WL_WIDEN_IMPL(text) L##text
#define WL_WIDEN(text) WL_WIDEN_IMPL(text)

#define WL_TRY_HR(expr, code, message)                                                        \
    do {                                                                                      \
        const long wlHr = static_cast<long>(expr);                                            \
        if (wlHr < 0) {                                                                       \
            return ::wl::fail((code), (message), WL_WIDEN(#expr), static_cast<std::int32_t>(wlHr)); \
        }                                                                                     \
    } while (false)
