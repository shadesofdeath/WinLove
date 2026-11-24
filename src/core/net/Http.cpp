#include "core/net/Http.h"

#include "base/Log.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
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

// The one WinHTTP session of the process (audit OPS-12): requests that share it reuse its
// connections (keep-alive) and its proxy lookup instead of a new TLS handshake each. A session is
// thread-safe and is never closed; the timeouts and TLS options are the same for every request.
// Null when WinHTTP could not open one: `error` then holds the reason and the next call retries.
HINTERNET sharedSession(DWORD& error) {
    static std::mutex guard;
    static HINTERNET session = nullptr;
    const std::scoped_lock lock(guard);
    error = ERROR_SUCCESS;
    if (!session) {
        session = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            error = GetLastError();
            return nullptr;
        }
        // Resolve 30 s, connect 30 s, send 60 s, receive 120 s: a slow mirror must not look dead.
        WinHttpSetTimeouts(session, 30000, 30000, 60000, 120000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    return session;
}

// The request handle, closed early when the token is cancelled (audit OPS-07): a synchronous
// WinHTTP call waits up to its timeout (minutes) on a dead network, and closing the request from
// another thread is the documented way to make it return at once. Ownership: the handle lives in
// one atomic slot and whoever exchanges it out closes it (the cancel callback or the destructor,
// never both). The destructor first waits for a running callback, so the callback never touches a
// dead object. Lives on the heap (the callback keeps its address) and is never moved.
class CancellableRequest {
public:
    CancellableRequest(HINTERNET request, const CancelToken& cancel) : m_handle(request), m_cancel(cancel) {
        // The token copy keeps the event alive for as long as the wait is registered.
        if (!RegisterWaitForSingleObject(&m_wait, m_cancel.event(), &CancellableRequest::onCancel, this, INFINITE,
                                         WT_EXECUTEONLYONCE)) {
            m_wait = nullptr; // no early wake-up then; cancellation is still seen between the calls
        }
    }
    CancellableRequest(const CancellableRequest&) = delete;
    CancellableRequest& operator=(const CancellableRequest&) = delete;
    ~CancellableRequest() {
        if (m_wait) {
            // INVALID_HANDLE_VALUE: returns only after a callback that is running has finished.
            UnregisterWaitEx(m_wait, INVALID_HANDLE_VALUE);
        }
        if (const HINTERNET h = m_handle.exchange(nullptr)) {
            WinHttpCloseHandle(h);
        }
    }

    // Null once cancelled: WinHTTP then fails the call ("invalid handle") and `cancelled()` says why.
    [[nodiscard]] HINTERNET get() const noexcept { return m_handle.load(); }
    [[nodiscard]] bool cancelled() const noexcept { return m_closedByCancel.load() || m_cancel.cancelled(); }

private:
    static void CALLBACK onCancel(void* context, BOOLEAN) {
        auto* self = static_cast<CancellableRequest*>(context);
        self->m_closedByCancel.store(true);
        if (const HINTERNET h = self->m_handle.exchange(nullptr)) {
            WinHttpCloseHandle(h);
        }
    }

    std::atomic<HINTERNET> m_handle;
    std::atomic<bool> m_closedByCancel{false};
    CancelToken m_cancel;
    HANDLE m_wait = nullptr;
};

// One request: connection + request (in the shared session), sent, response headers received.
struct Request {
    Handle connection;
    std::unique_ptr<CancellableRequest> request; // declared after `connection`: closed before it
    int status = 0;

    [[nodiscard]] HINTERNET handle() const noexcept { return request ? request->get() : nullptr; }
    [[nodiscard]] bool cancelled() const noexcept { return request && request->cancelled(); }
};

Error cancelledError(std::wstring_view url) {
    return Error{ErrorCode::Cancelled, L"download cancelled", std::wstring(url), 0};
}

// Per-request settings on a request handle that is not sent yet (D-140). Each one is best effort: a
// refused option leaves the session's default, never a failed request.
void applyOptions(HINTERNET request, const HttpOptions& options) {
    if (options.timeoutMs > 0) {
        const int t = static_cast<int>(options.timeoutMs);
        WinHttpSetTimeouts(request, t, t, t, t);
    }
    if (!options.userAgent.empty()) {
        // REPLACE | ADD: the session's agent goes, this one is the only User-Agent line.
        const std::wstring line = L"User-Agent: " + options.userAgent + L"\r\n";
        WinHttpAddRequestHeaders(request, line.c_str(), static_cast<DWORD>(-1L),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    if (options.noCookies) {
        DWORD feature = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));
    }
}

std::wstring queryHeader(HINTERNET request, DWORD info, const wchar_t* name = WINHTTP_HEADER_NAME_BY_INDEX) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, info, name, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring text(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, info, name, text.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    text.resize(size / sizeof(wchar_t));
    while (!text.empty() && (text.back() == L'\0' || text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    return text;
}

Result<Request> send(std::wstring_view url, const wchar_t* verb, std::wstring_view extraHeaders,
                     std::string_view body, const CancelToken& cancel, const HttpOptions* options = nullptr) {
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
    if (cancel.cancelled()) {
        return std::unexpected(cancelledError(url));
    }
    DWORD sessionError = ERROR_SUCCESS;
    const HINTERNET session = sharedSession(sessionError);
    if (!session) {
        return std::unexpected(netError(sessionError, L"WinHTTP session could not be opened", url));
    }
    Request r;
    r.connection = Handle(WinHttpConnect(session, host, parts.nPort, 0));
    if (!r.connection) {
        return std::unexpected(netError(GetLastError(), L"could not connect", url));
    }
    const std::wstring object = std::wstring(path) + extra;
    const HINTERNET opened = WinHttpOpenRequest(r.connection.h, verb, object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!opened) {
        return std::unexpected(netError(GetLastError(), L"could not open the request", url));
    }
    r.request = std::make_unique<CancellableRequest>(opened, cancel); // owns `opened` from here on
    if (options) {
        applyOptions(opened, *options);
    }
    const std::wstring headerText(extraHeaders);
    const BOOL sent = WinHttpSendRequest(
        r.handle(), headerText.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerText.c_str(),
        headerText.empty() ? 0 : static_cast<DWORD>(-1L), body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(r.handle(), nullptr)) {
        const DWORD code = GetLastError();
        if (r.cancelled()) {
            return std::unexpected(cancelledError(url)); // the request was closed under the call
        }
        return std::unexpected(netError(code, L"the server did not answer", url));
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(r.handle(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (r.cancelled()) {
        return std::unexpected(cancelledError(url)); // not "HTTP 0": the status of a closed request is not read
    }
    r.status = static_cast<int>(status);
    return r;
}

// The address the request ended at (after redirects WinHTTP followed); empty when unknown.
std::wstring requestUrl(const Request& r) {
    DWORD size = 0;
    WinHttpQueryOption(r.handle(), WINHTTP_OPTION_URL, nullptr, &size);
    if (size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring text(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(r.handle(), WINHTTP_OPTION_URL, text.data(), &size)) {
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
    if (!WinHttpQueryHeaders(r.handle(), WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size,
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
        if (!WinHttpReadData(r.handle(), buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            const DWORD code = GetLastError();
            if (r.cancelled()) {
                return std::unexpected(cancelledError(url)); // the request was closed under the read
            }
            return std::unexpected(netError(code, L"the connection broke off", url));
        }
        if (r.cancelled()) {
            return std::unexpected(cancelledError(url)); // nothing read after a cancel is kept
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
                           std::string_view body, const CancelToken& cancel, const HttpOptions* options = nullptr) {
    auto r = send(url, verb, headers, body, cancel, options);
    if (!r) {
        return std::unexpected(r.error());
    }
    HttpResponse response{r->status, {}, {}, {}};
    if (options && options->keepRequestHeaders) {
        response.sentHeaders = queryHeader(r->handle(), WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_REQUEST_HEADERS);
    }
    if (options) {
        response.rateLimitRemaining = queryHeader(r->handle(), WINHTTP_QUERY_CUSTOM, L"X-RateLimit-Remaining");
    }
    const std::size_t limit = options && options->maxBytes ? options->maxBytes : (64u << 20); // a page, not a file
    auto read = readBody(*r, cancel, url, [&](const char* data, DWORD size) {
        response.body.append(data, size);
        return response.body.size() < limit;
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

Result<HttpResponse> httpGet(std::wstring_view url, const HttpOptions& options, const CancelToken& cancel) {
    return fetch(url, L"GET", options.headers, {}, cancel, &options);
}

Result<HttpResponse> httpPost(std::wstring_view url, std::string_view body, std::wstring_view contentType,
                              const CancelToken& cancel) {
    const std::wstring headers = L"Content-Type: " + std::wstring(contentType) + L"\r\n";
    return fetch(url, L"POST", headers, body, cancel);
}

Result<HttpResponse> httpPost(std::wstring_view url, std::string_view body, std::wstring_view contentType,
                              const HttpOptions& options, const CancelToken& cancel) {
    const std::wstring headers = L"Content-Type: " + std::wstring(contentType) + L"\r\n" + options.headers;
    return fetch(url, L"POST", headers, body, cancel, &options);
}

Result<HttpResponse> httpPostForm(std::wstring_view url, std::string_view form, const CancelToken& cancel) {
    return fetch(url, L"POST", L"Content-Type: application/x-www-form-urlencoded\r\n", form, cancel);
}

Result<std::uint64_t> httpDownload(std::wstring_view url, const std::filesystem::path& target,
                                   std::uint64_t expectedSize, const TaskContext& task,
                                   const std::function<void(std::uint64_t, std::uint64_t)>& bytes,
                                   const std::function<bool(std::wstring_view)>& acceptFinalUrl, const HttpOptions* options) {
    const std::filesystem::path part = target.wstring() + L".part";
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::uint64_t have = std::filesystem::exists(part, ec) ? std::filesystem::file_size(part, ec) : 0;
    if (ec || (expectedSize && have >= expectedSize)) {
        have = 0; // unreadable or already too long: start over
    }
    const std::wstring range = have ? std::format(L"Range: bytes={}-\r\n", have) : std::wstring();
    auto r = send(url, L"GET", range, {}, task.cancel, options);
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
        if (r->status == 416 && have) {
            // The .part is as long as (or longer than) what the server has now: no later attempt could
            // resume it, each would get this answer again (AUDIT-2026-10 B14). The next one starts over.
            std::filesystem::remove(part, ec);
        }
        return fail(ErrorCode::IoError, std::format(L"the server answered HTTP {}", r->status), std::wstring(url));
    }
    // Without an expected size, the Content-Length of this answer says how long the file is (audit
    // OPS-06); a chunked answer has none and the total stays unknown (0), as before.
    const std::uint64_t announced = contentLength(*r);
    const std::uint64_t total = expectedSize ? expectedSize : (announced ? have + announced : 0);
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
        // Shorter: the server closed the connection early (Microsoft's CDN does that every few MB on
        // some files) — the .part stays and the next attempt resumes it. Longer: not this file.
        if (done > expectedSize) {
            std::filesystem::remove(part, ec);
        }
        return fail(ErrorCode::IoError, std::format(L"download ended at {} of {} bytes", done, expectedSize),
                    std::wstring(url));
    }
    if (!expectedSize && total && done < total) {
        // The server closed the connection before the length it announced: not the whole file. The
        // .part stays and the next attempt resumes it (audit OPS-06).
        return fail(ErrorCode::IoError, std::format(L"download ended at {} of {} bytes", done, total),
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
