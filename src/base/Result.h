#pragma once
// Error model shared by every layer. Exceptions never cross a module boundary;
// fallible functions return wl::Result<T>.
#include <cstdint>
#include <expected>
#include <string>

namespace wl {

enum class ErrorCode : std::uint16_t {
    Unknown,
    InvalidArgument,
    NotFound,
    AccessDenied,
    Cancelled,
    IoError,
    ParseError,
    Unsupported,
    DismFailure,
    WimFailure,
    RenderFailure,
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::wstring message;  // human readable, English (UI maps codes to localized text)
    std::wstring context;  // what was being done: operation + target
    std::int32_t hresult = 0;
};

template <class T>
using Result = std::expected<T, Error>;

// return wl::fail(ErrorCode::NotFound, L"install.wim missing", path);
[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::wstring message,
                                                 std::wstring context = {}, std::int32_t hresult = 0) {
    return std::unexpected<Error>(Error{code, std::move(message), std::move(context), hresult});
}

[[nodiscard]] const wchar_t* codeName(ErrorCode code) noexcept;

// "NotFound: install.wim missing (opening C:\x.iso) [0x80070002]"
[[nodiscard]] std::wstring describe(const Error& error);

} // namespace wl
