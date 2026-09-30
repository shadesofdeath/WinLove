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
};

// GET / POST (application/x-www-form-urlencoded). A status other than 2xx is still a response;
// errors are for network failures, bad URLs and cancellation.
[[nodiscard]] Result<HttpResponse> httpGet(std::wstring_view url, const CancelToken& cancel = {});
[[nodiscard]] Result<HttpResponse> httpPostForm(std::wstring_view url, std::string_view form,
                                                const CancelToken& cancel = {});

// Downloads `url` to `target`. The bytes go to "<target>.part" first — an interrupted download
// continues from there with a Range request (the server may refuse: then it starts over) — and
// the file is renamed to `target` when complete. Progress: fraction of `expectedSize` (or of the
// Content-Length when 0). An existing `target` is replaced.
// `bytes` (optional): bytes in the file so far and the total (0 = unknown), for progress over
// several files.
[[nodiscard]] Result<std::uint64_t> httpDownload(std::wstring_view url, const std::filesystem::path& target,
                                                 std::uint64_t expectedSize, const TaskContext& task,
                                                 const std::function<void(std::uint64_t, std::uint64_t)>& bytes = {});

// "a=1&b=x%20y": percent-encoding of UTF-8 text for a form body or a query string.
[[nodiscard]] std::string urlEncode(std::string_view text);

} // namespace wl::core
