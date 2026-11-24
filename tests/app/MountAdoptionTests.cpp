// 2026-11-11 (the owner: an image another tool mounted, double-clicked on Kaynak, stayed at
// "Bağlanıyor %0" and then said "folder has no sources\install.wim"): taking over a mount made
// elsewhere — what DISM's record decides, where its source opens from ("\\?\" paths, "Sources" in
// any case, an image outside any setup folder), and whose folder the mount is.
#include "app/state/AppState.h"
#include "base/Path.h"
#include "core/image/Source.h"
#include "core/image/dism/MountHealth.h"

#include <doctest.h>

#include <fstream>

using namespace wl;
using core::AdoptStep;
using core::DismMountStatus;
using core::MountState;

namespace {

std::filesystem::path base(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"adopt" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

void touch(const std::filesystem::path& file, const char* text = "not a wim") {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::optional<core::MountInfo> record(DismMountStatus status, std::filesystem::path mount = L"\\\\?\\C:\\Tools\\Mounts\\Tmp",
                                      std::filesystem::path image = L"\\\\?\\C:\\Tools\\Cache\\26200\\Sources\\install.wim") {
    return core::MountInfo{std::move(mount), std::move(image), 2, false, status};
}

} // namespace

TEST_CASE("adoption: DISM's status decides — Ok used, NeedsRemount remounted first, the rest refused untouched") {
    // What another tool's mount can be in, as classifyMount reads DISM's record and the file system.
    CHECK(core::adoptionStep(core::classifyMount(record(DismMountStatus::Ok), true, true, false)) == AdoptStep::Use);
    CHECK(core::adoptionStep(core::classifyMount(record(DismMountStatus::NeedsRemount), true, true, false)) == AdoptStep::Remount);
    CHECK(core::adoptionStep(core::classifyMount(record(DismMountStatus::Invalid), true, true, false)) == AdoptStep::Refuse);
    // The image file gone: neither a remount nor a save can work.
    CHECK(core::adoptionStep(core::classifyMount(record(DismMountStatus::Ok), false, true, false)) == AdoptStep::Refuse);
    CHECK(core::adoptionStep(core::classifyMount(record(DismMountStatus::NeedsRemount), false, true, false)) == AdoptStep::Refuse);
    // Nothing mounted there (any more): leftovers, someone's files, an empty folder.
    CHECK(core::adoptionStep(core::classifyMount(std::nullopt, false, true, true)) == AdoptStep::Refuse);
    CHECK(core::adoptionStep(core::classifyMount(std::nullopt, false, true, false)) == AdoptStep::Refuse);
    CHECK(core::adoptionStep(core::classifyMount(std::nullopt, false, false, false)) == AdoptStep::Refuse);
    for (const auto state : {MountState::Invalid, MountState::ImageMissing, MountState::Orphaned, MountState::Foreign, MountState::Free}) {
        CHECK(core::adoptionStep(state) == AdoptStep::Refuse);
    }
}

TEST_CASE("adoption: the \\\\?\\ prefix DISM answers with is dropped; a too long path keeps it") {
    CHECK(withoutLongPathPrefix(L"\\\\?\\C:\\ProgramData\\Tool\\Mounts\\Tmp") == std::filesystem::path(L"C:\\ProgramData\\Tool\\Mounts\\Tmp"));
    CHECK(withoutLongPathPrefix(L"\\\\?\\UNC\\server\\share\\install.wim") == std::filesystem::path(L"\\\\server\\share\\install.wim"));
    CHECK(withoutLongPathPrefix(L"C:\\plain\\install.wim") == std::filesystem::path(L"C:\\plain\\install.wim"));
    CHECK(withoutLongPathPrefix(L"\\\\?\\Volume{0}\\x") == std::filesystem::path(L"\\\\?\\Volume{0}\\x")); // no drive: kept
    const std::wstring longPath = L"\\\\?\\C:\\" + std::wstring(300, L'a') + L"\\install.wim";
    CHECK(withoutLongPathPrefix(longPath).native() == longPath);
}

TEST_CASE("adoption: the source of a mounted image — setup media only when it is setup media, else the WIM itself") {
    // The other tool's cache: <build>\Sources\install.wim with a capital S and nothing else around it.
    const auto cache = base(L"cache");
    const auto image = cache / L"26200.9818.TEST_X64FRE_TR-TR" / L"Sources" / L"install.wim";
    touch(image);
    CHECK(core::sourceForMountedImage(image) == image);
    // As DISM names it: with "\\?\" — the same file, without the prefix.
    CHECK(core::sourceForMountedImage(L"\\\\?\\" + image.wstring()) == image);
    // Setup media around it (setup.exe, or sources\boot.wim): the folder, so the ISO can be built from it.
    touch(cache / L"26200.9818.TEST_X64FRE_TR-TR" / L"setup.exe");
    CHECK(core::sourceForMountedImage(L"\\\\?\\" + image.wstring()) == cache / L"26200.9818.TEST_X64FRE_TR-TR");
    const auto media = base(L"media");
    touch(media / L"sources" / L"install.wim");
    touch(media / L"sources" / L"boot.wim");
    CHECK(core::sourceForMountedImage(media / L"sources" / L"install.wim") == media);
    // Not the install image (a mounted boot.wim, a renamed WIM, a WIM in any other folder): the file itself.
    CHECK(core::sourceForMountedImage(media / L"sources" / L"boot.wim") == media / L"sources" / L"boot.wim");
    const auto loose = base(L"loose") / L"Win11_Pro.wim";
    touch(loose);
    CHECK(core::sourceForMountedImage(loose) == loose);
}

TEST_CASE("adoption: a setup folder under \\\\?\\ is read — the reported 'folder has no sources\\install.wim'") {
    // openSource joined "sources/install.wim" with a '/': under "\\?\" Windows does not turn it into
    // a separator and the file was never found. Now it is found (a junk file: a WIM error, not NotFound).
    const auto media = base(L"prefixed");
    touch(media / L"Sources" / L"install.wim");
    auto opened = core::openSource(L"\\\\?\\" + media.wstring());
    REQUIRE_FALSE(opened);
    CHECK(opened.error().message.find(L"folder has no sources") == std::wstring::npos);
    // openMountedImageSource falls back to the image file when the folder does not open; a junk file
    // fails there too — with the file's own error, never the missing-folder one.
    touch(media / L"setup.exe");
    auto mounted = core::openMountedImageSource(L"\\\\?\\" + (media / L"Sources" / L"install.wim").wstring());
    REQUIRE_FALSE(mounted);
    CHECK(mounted.error().message.find(L"folder has no sources") == std::wstring::npos);
}

TEST_CASE("adoption: WinLove's own mount folders vs another tool's — said on the pages, saving touches that folder only") {
    const auto dir = base(L"state");
    app::AppState state{dir / L"recent.json", dir / L"settings.json"};
    const auto own = state.settings().mountDirectory();
    CHECK(state.isOwnMountFolder(own));
    CHECK(state.isOwnMountFolder(L"\\\\?\\" + own.wstring()));         // DISM's spelling of it
    CHECK(state.isOwnMountFolder(own.wstring() + L"\\"));               // a trailing separator
    CHECK(state.isOwnMountFolder(app::AppSettings::legacyMountDirectory()));
    CHECK(state.isOwnMountFolder(state.settings().vhdMountDirectory()));
    CHECK_FALSE(state.isOwnMountFolder(L"C:\\ProgramData\\Tool\\Mounts\\Tmp"));
    CHECK_FALSE(state.mountedElsewhere()); // nothing mounted
    state.setMounted(app::MountedImage{own, L"C:\\WinLove\\work\\sources\\install.wim", 1, L"Windows 11 Pro"});
    CHECK_FALSE(state.mountedElsewhere());
    state.setMounted(app::MountedImage{L"C:\\ProgramData\\Tool\\Mounts\\Tmp", L"C:\\Tool\\Cache\\Sources\\install.wim", 2, L"Windows 11 Pro"});
    CHECK(state.mountedElsewhere());
}
