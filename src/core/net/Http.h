#pragma once
// Small HTTPS client over WinHTTP (no third-party code): what the update catalog needs — a page
// GET, a form POST and a resumable file download (D-046). The system proxy is used automatically.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace wl::core {

struct HttpResponse {
    int status = 0;
    std::string body; // bytes as received (the catalog sends UTF-8)
    // HttpOptions::keepRequestHeaders: the request's headers as WinHTTP sent them ("GET /… HTTP/1.1\r\n
    // Host: …\r\nUser-Agent: …"), to show what left the machine (D-140, wlcli update-check --verbose).
    std::wstring sentHeaders;
    std::wstring rateLimitRemaining; // the "X-RateLimit-Remaining" header, when the server sent one
};

// D-140: what one request may change from the shared session's defaults.
struct HttpOptions {
    std::wstring userAgent;        // replaces the session's agent for this request ("WinLove/1.3.0")
    std::wstring headers;          // more request headers, each ending in "\r\n"
    unsigned timeoutMs = 0;        // resolve / connect / send / receive, each (0: the session's 30-120 s)
    bool noCookies = true;         // nothing remembered from an earlier answer goes out again
    bool keepRequestHeaders = false;
    std::size_t maxBytes = 0;      // a longer body is an error (0: the usual 64 MB of a page)
};

// GET / POST (application/x-www-form-urlencoded). A status other than 2xx is still a response;
// errors are for network failures, bad URLs and cancellation.
[[nodiscard]] Result<HttpResponse> httpGet(std::wstring_view url, const CancelToken& cancel = {});
[[nodiscard]] Result<HttpResponse> httpGet(std::wstring_view url, const HttpOptions& options, const CancelToken& cancel = {});
[[nodiscard]] Result<HttpResponse> httpPostForm(std::wstring_view url, std::string_view form,
                                                const CancelToken& cancel = {});
// Any body with its content type ("application/json", "application/soap+xml; charset=utf-8").
[[nodiscard]] Result<HttpResponse> httpPost(std::wstring_view url, std::string_view body, std::wstring_view contentType,
                                            const CancelToken& cancel = {});

// The same with this request's User-Agent / timeouts / size cap (D-149: the official ESD catalog's query).
[[nodiscard]] Result<HttpResponse> httpPost(std::wstring_view url, std::string_view body, std::wstring_view contentType,
                                            const HttpOptions& options, const CancelToken& cancel = {});

// Downloads `url` to `target`. The bytes go to "<target>.part" first — an interrupted download
// (a read error, or a server that closed the connection before `expectedSize`: an error too)
// continues from there with a Range request (the server may refuse: then it starts over) — and
// the file is renamed to `target` when complete. Progress: fraction of `expectedSize` (or of the
// Content-Length when 0). An existing `target` is replaced.
// `bytes` (optional): bytes in the file so far and the total (0 = unknown), for progress over
// several files. `acceptFinalUrl` (optional): the address the request ended at after redirects;
// false refuses the download before a byte is written. `options` (optional, D-145): this request's
// User-Agent / cookies / timeouts (the self-update says "WinLove/<version>").
[[nodiscard]] Result<std::uint64_t> httpDownload(std::wstring_view url, const std::filesystem::path& target,
                                                 std::uint64_t expectedSize, const TaskContext& task,
                                                 const std::function<void(std::uint64_t, std::uint64_t)>& bytes = {},
                                                 const std::function<bool(std::wstring_view)>& acceptFinalUrl = {},
                                                 const HttpOptions* options = nullptr);

// "a=1&b=x%20y": percent-encoding of UTF-8 text for a form body or a query string.
[[nodiscard]] std::string urlEncode(std::string_view text);

} // namespace wl::core
