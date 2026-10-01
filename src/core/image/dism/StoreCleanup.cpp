#include "core/image/dism/StoreCleanup.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <format>

namespace wl::core {

namespace {

using Json = nlohmann::json;

} // namespace

std::string storeCleanupToJson(const StoreCleanupOptions& options) {
    return Json{{"title", utf8::fromWide(options.title)}, {"resetBase", options.resetBase}}.dump();
}

Result<StoreCleanupOptions> storeCleanupFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"not a cleanup operation");
    }
    StoreCleanupOptions options;
    try {
        options.title = utf8::toWide(doc.value("title", std::string{}));
        options.resetBase = doc.value("resetBase", true);
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed cleanup operation", utf8::toWide(e.what()));
    }
    return options;
}

namespace {

const wchar_t* cleanupArguments(bool resetBase) noexcept {
    return resetBase ? L"/Cleanup-Image /StartComponentCleanup /ResetBase" : L"/Cleanup-Image /StartComponentCleanup";
}

} // namespace

std::wstring storeCleanupCommandLine(const std::filesystem::path& dismExe, const std::filesystem::path& mountDir,
                                     bool resetBase) {
    return dismExeCommandLine(dismExe, mountDir, cleanupArguments(resetBase));
}

Result<void> cleanupComponentStore(DismSession& session, bool resetBase, const TaskContext& task) {
    if (auto go = task.cancel.check(L"component store cleanup"); !go) {
        return go;
    }
    const auto run = runDismExe(session, cleanupArguments(resetBase),
                                [&](double percent) { task.report(percent, L"StartComponentCleanup"); });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode == 0x800F0806) { // CBS_E_PENDING
        return fail(ErrorCode::Unsupported,
                    L"component store cleanup skipped: the image has pending operations (a feature or update of this "
                    L"run finishes at first boot); the image itself is unchanged by this step",
                    run->message, static_cast<std::int32_t>(run->exitCode));
    }
    if (run->exitCode != 0) {
        return fail(ErrorCode::IoError, L"component store cleanup failed",
                    run->message.empty() ? std::format(L"dism.exe exit code 0x{:08X}", run->exitCode) : run->message,
                    static_cast<std::int32_t>(run->exitCode));
    }
    task.report(1.0, L"StartComponentCleanup");
    return {};
}

} // namespace wl::core
