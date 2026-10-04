#include "core/image/LanguageInstall.h"

#include "base/Log.h"
#include "base/Text.h"
#include "core/image/LanguagePacks.h"
#include "core/image/dism/Dism.h"
#include "core/image/wim/WimGapi.h"
#include "core/system/Files.h"

#include <windows.h>

#include <atomic>
#include <format>

namespace wl::core {

namespace {

// A fresh folder under %TEMP%\WinLove\lang, removed when the step is done.
struct Scratch {
    std::filesystem::path dir;
    Scratch() {
        static std::atomic<unsigned> counter{0};
        dir = tempFolder() / L"WinLove" / L"lang" / std::format(L"{}-{}", GetCurrentProcessId(), counter++);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

} // namespace

Result<void> addLanguagePackage(DismSession& session, const std::filesystem::path& file, const TaskContext& task) {
    const auto info = classifyLanguageFile(file);
    if (text::lower(file.extension().wstring()) == L".esd") {
        if (info.kind != LanguagePackFile::Kind::LanguagePack) {
            return fail(ErrorCode::InvalidArgument, L"not a language pack ESD", file.wstring());
        }
        Scratch scratch;
        const auto package = scratch.dir / L"package";
        if (auto applied = applyImage(file, 1, package, TaskContext{task.cancel, {}}); !applied) {
            return applied;
        }
        log::info("apply", std::format(L"language pack {} expanded into {}", info.language, package.wstring()));
        return session.addPackage(package, task);
    }
    const std::wstring wanted = cbsFileName(info);
    if (wanted.empty() || text::lower(wanted) == text::lower(file.filename().wstring())) {
        return session.addPackage(file, task);
    }
    Scratch scratch;
    const auto staged = scratch.dir / wanted;
    std::error_code ec;
    std::filesystem::copy_file(file, staged, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot copy the language file to the scratch folder", staged.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()))));
    }
    log::info("apply", std::format(L"{} staged as {}", file.filename().wstring(), wanted));
    return session.addPackage(staged, task);
}

} // namespace wl::core
