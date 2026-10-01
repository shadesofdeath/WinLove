#include "core/net/Http.h"

#include "base/Log.h"

#include <windows.h>
#include <winhttp.h>

#include <format>
#include <fstream>
#include <vector>

namespace wl::core {

namespace {

constexpr wchar_t kAgent[] = L"WinLove/1.0 (Windows image customization)";
constexpr DWORD kChunk = 1u << 20;

Error netError(DWORD code, std::wstring what, std::wstring_view url) {
    const auto errorCode = code == ERROR_WINHTTP_TIMEOUT || code == ERROR_WINHTTP_CANNOT_CONNECT ||
                                   code == ERROR_WINHTTP_NAME_NOT_RESOLVED || code == ERROR_WINHTTP_CONNECTION_ERROR
                               ? ErrorCode::IoError
                               : ErrorCode::Unknown;
    return Error{errorCode, std::move(what), std::wstring(url), static_cast<std::int32_t>(HRESULT_FROM_WIN32(code))};
}

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET handle) : h(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h(std::exchange(o.h, nullptr)) {}
    Handle& operator=(Handle&& o) noexcept {
        if (this != &o) {
            if (h) {
                WinHttpCloseHandle(h);
            }
            h = std::exchange(o.h, nullptr);
        }
        return *this;
    }
    ~Handle() {
        if (h) {
            WinHttpCloseHandle(h);
        }
    }
    explicit operator bool() const noexcept { return h != nullptr; }
};

// One request: session + connection + request, sent, response headers received.
struct Request {
    Handle session;
    Handle connection;
    Handle request;
    int status = 0;
};

Result<Request> send(std::wstring_view url, const wchar_t* verb, std::wstring_view extraHeaders,
                     std::string_view body) {
    std::wstring copy(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    wchar_t extra[4096] = {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(copy.c_str(), static_cast<DWORD>(copy.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP)) {
        return fail(ErrorCode::InvalidArgument, L"not an http(s) address", copy);
    }
    Request r;
    r.session = Handle(WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0));
    if (!r.session) {
        return std::unexpected(netError(GetLastError(), L"WinHTTP session could not be opened", url));
    }
    // Resolve 30 s, connect 30 s, send 60 s, receive 120 s: a slow mirror must not look dead.
    WinHttpSetTimeouts(r.session.h, 30000, 30000, 60000, 120000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(r.session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    r.connection = Handle(WinHttpConnect(r.session.h, host, parts.nPort, 0));
    if (!r.connection) {
        return std::unexpected(netError(GetLastError(), L"could not connect", url));
    }
    const std::wstring object = std::wstring(path) + extra;
    r.request = Handle(WinHttpOpenRequest(r.connection.h, verb, object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    if (!r.request) {
        return std::unexpected(netError(GetLastError(), L"could not open the request", url));
    }
    const BOOL sent = WinHttpSendRequest(
        r.request.h, extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : std::wstring(extraHeaders).c_str(),
        extraHeaders.empty() ? 0 : static_cast<DWORD>(-1L), body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(r.request.h, nullptr)) {
        return std::unexpected(netError(GetLastError(), L"the server did not answer", url));
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(r.request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    return r;
}

// The address the request ended at (after redirects WinHTTP followed); empty when unknown.
std::wstring requestUrl(const Request& r) {
    DWORD size = 0;
    WinHttpQueryOption(r.request.h, WINHTTP_OPTION_URL, nullptr, &size);
    if (size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring text(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(r.request.h, WINHTTP_OPTION_URL, text.data(), &size)) {
        return {};
    }
    text.resize(size / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    return text;
}

std::uint64_t contentLength(const Request& r) {
    wchar_t text[32] = {};
    DWORD size = sizeof(text);
    if (!WinHttpQueryHeaders(r.request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    return std::wcstoull(text, nullptr, 10);
}

// Reads the body in chunks; `sink` returns false to stop (write failure).
template <class Sink>
Result<void> readBody(const Request& r, const CancelToken& cancel, std::wstring_view url, Sink&& sink) {
    std::vector<char> buffer(kChunk);
    for (;;) {
        if (cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"download cancelled", std::wstring(url));
        }
        DWORD got = 0;
        if (!WinHttpReadData(r.request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            return std::unexpected(netError(GetLastError(), L"the connection broke off", url));
        }
        if (got == 0) {
            return {};
        }
        if (!sink(buffer.data(), got)) {
            return fail(ErrorCode::IoError, L"could not write the downloaded data", std::wstring(url));
        }
    }
}

Result<HttpResponse> fetch(std::wstring_view url, const wchar_t* verb, std::wstring_view headers,
                           std::string_view body, const CancelToken& cancel) {
    auto r = send(url, verb, headers, body);
    if (!r) {
        return std::unexpected(r.error());
    }
    HttpResponse response{r->status, {}};
    auto read = readBody(*r, cancel, url, [&](const char* data, DWORD size) {
        response.body.append(data, size);
        return response.body.size() < (64u << 20); // a page, not a file
    });
    if (!read) {
        return std::unexpected(read.error());
    }
    return response;
}

} // namespace

Result<HttpResponse> httpGet(std::wstring_view url, const CancelToken& cancel) {
    return fetch(url, L"GET", {}, {}, cancel);
}

Result<HttpResponse> httpPostForm(std::wstring_view url, std::string_view form, const CancelToken& cancel) {
    return fetch(url, L"POST", L"Content-Type: application/x-www-form-urlencoded\r\n", form, cancel);
}

Result<std::uint64_t> httpDownload(std::wstring_view url, const std::filesystem::path& target,
                                   std::uint64_t expectedSize, const TaskContext& task,
                                   const std::function<void(std::uint64_t, std::uint64_t)>& bytes,
                                   const std::function<bool(std::wstring_view)>& acceptFinalUrl) {
    const std::filesystem::path part = target.wstring() + L".part";
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::uint64_t have = std::filesystem::exists(part, ec) ? std::filesystem::file_size(part, ec) : 0;
    if (ec || (expectedSize && have >= expectedSize)) {
        have = 0; // unreadable or already too long: start over
    }
    const std::wstring range = have ? std::format(L"Range: bytes={}-\r\n", have) : std::wstring();
    auto r = send(url, L"GET", range, {});
    if (!r) {
        return std::unexpected(r.error());
    }
    if (acceptFinalUrl) {
        const std::wstring finalUrl = requestUrl(*r);
        if (finalUrl.empty() || !acceptFinalUrl(finalUrl)) {
            return fail(ErrorCode::AccessDenied, L"the download was redirected to an untrusted address",
                        finalUrl.empty() ? std::wstring(url) : finalUrl);
        }
    }
    if (r->status == 200) {
        have = 0; // the server sends the whole file (no range support)
    } else if (r->status != 206 || !have) {
        return fail(ErrorCode::IoError, std::format(L"the server answered HTTP {}", r->status), std::wstring(url));
    }
    const std::uint64_t total = expectedSize ? expectedSize : have + contentLength(*r);
    if (have) {
        log::info("net", std::format(L"resuming {} at {} bytes", target.filename().wstring(), have));
    }
    std::ofstream out(part, std::ios::binary | (have ? std::ios::app : std::ios::trunc));
    if (!out) {
        return fail(ErrorCode::IoError, L"cannot write the download", part.wstring());
    }
    std::uint64_t done = have;
    task.report(total ? static_cast<double>(done) / static_cast<double>(total) : -1.0, L"download");
    if (bytes) {
        bytes(done, total);
    }
    auto read = readBody(*r, task.cancel, url, [&](const char* data, DWORD size) {
        out.write(data, size);
        done += size;
        task.report(total ? static_cast<double>(done) / static_cast<double>(total) : -1.0, L"download");
        if (bytes) {
            bytes(done, total);
        }
        return static_cast<bool>(out);
    });
    out.close();
    if (!read) {
        return std::unexpected(read.error()); // the .part stays for the next attempt
    }
    if (!out) {
        return fail(ErrorCode::IoError, L"cannot write the download", part.wstring());
    }
    if (expectedSize && done != expectedSize) {
        std::filesystem::remove(part, ec);
        return fail(ErrorCode::IoError, std::format(L"download ended at {} of {} bytes", done, expectedSize),
                    std::wstring(url));
    }
    std::filesystem::remove(target, ec);
    std::filesystem::rename(part, target, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot move the download into place", target.wstring(), ec.value());
    }
    return done;
}

std::string urlEncode(std::string_view text) {
    std::string out;
    for (const unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out += std::format("%{:02X}", c);
        }
    }
    return out;
}

} // namespace wl::core
