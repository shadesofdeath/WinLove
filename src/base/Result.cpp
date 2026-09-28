#include "base/Result.h"

#include <cstdio>

namespace wl {

const wchar_t* codeName(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Unknown: return L"Unknown";
    case ErrorCode::InvalidArgument: return L"InvalidArgument";
    case ErrorCode::NotFound: return L"NotFound";
    case ErrorCode::AccessDenied: return L"AccessDenied";
    case ErrorCode::Cancelled: return L"Cancelled";
    case ErrorCode::IoError: return L"IoError";
    case ErrorCode::ParseError: return L"ParseError";
    case ErrorCode::Unsupported: return L"Unsupported";
    case ErrorCode::DismFailure: return L"DismFailure";
    case ErrorCode::WimFailure: return L"WimFailure";
    case ErrorCode::RenderFailure: return L"RenderFailure";
    }
    return L"?";
}

std::wstring describe(const Error& error) {
    std::wstring text = codeName(error.code);
    if (!error.message.empty()) {
        text += L": ";
        text += error.message;
    }
    if (!error.context.empty()) {
        text += L" (";
        text += error.context;
        text += L")";
    }
    if (error.hresult != 0) {
        wchar_t hr[16];
        std::swprintf(hr, 16, L" [0x%08X]", static_cast<unsigned>(error.hresult));
        text += hr;
    }
    return text;
}

} // namespace wl
