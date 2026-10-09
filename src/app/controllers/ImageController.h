#pragma once
// Image operations for P02 (docs/pages/02-images.md): prepare (ISO → work folder), mount,
// unmount, export, ESD → WIM, delete / rename editions, verify, clean up mounts, adopt an existing
// mount at startup.
// Lives on the UI thread; heavy work runs on AppState::engine(); results come back through
// `postToUi`. Knows nothing about widgets: it reports through Events.
#include "app/generated/StringKeys.g.h"
#include "app/state/AppState.h"
#include "base/Result.h"
#include "core/image/WorkCopy.h"
#include "core/image/dism/Edition.h"
#include "core/image/wim/WimVerify.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

// The source to open for a mounted WIM: the setup folder for ...\sources\install.wim, else the WIM.
[[nodiscard]] std::filesystem::path sourceForMountedImage(const std::filesystem::path& imagePath);

class ImageController {
public:
    enum class Failure : std::uint8_t { Mount, Unmount, Export, Delete, Prepare, Cleanup, Rename, Verify, Editions };

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(Failure, const Error&)> failed;
        std::function<void(Str title, std::wstring detail)> succeeded;       // success toast
        std::function<void(Str title)> refused;                                // busy / not allowed toast
        std::function<void(std::wstring relaunchArguments)> needsAdmin;        // show s4, relaunch with these args
        // A mount from a previous run is still attached: open `source`, then show it as mounted
        // (`edition` is left empty; the shell fills it from the opened source).
        std::function<void(std::filesystem::path source, MountedImage mounted)> restored;
        std::function<void()> mounted; // a fresh mount is up: time to read its contents (PreloadController)
        // Every stream of `file` was read: sound, or how many are damaged.
        std::function<void(const core::WimVerifyReport& report, std::wstring file)> verified;
        // An ISO's work copy that cannot just be used (core::WorkCopyState Modified / Unknown /
        // OtherSource, audit A5): the shell asks. proceed(true) extracts the ISO again over it,
        // proceed(false) uses the folder as it is. Not set: Modified / Unknown are used as they
        // are (nothing is lost), another ISO's copy is refused.
        std::function<void(core::WorkCopyState state, std::filesystem::path folder, std::function<void(bool fresh)> proceed)>
            workCopyChoice;
    };

    ImageController(AppState& state, Events events);
    ~ImageController();

    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool canMount() const;          // source mountable (not ESD), nothing mounted, idle
    // Why the image file cannot be changed now (busy, mounted, ESD / split image); empty: it can.
    [[nodiscard]] std::optional<Str> editRefusal() const;
    // Why editions cannot be deleted now: the above, or one edition left.
    [[nodiscard]] std::optional<Str> deleteRefusal() const;
    // Why the image cannot be verified (busy, ESD); empty: it can — mounted or not, ISO or file.
    [[nodiscard]] std::optional<Str> verifyRefusal() const;
    [[nodiscard]] bool canDelete() const { return !deleteRefusal(); }
    [[nodiscard]] bool isEsdSource() const;
    // D-100: a solid / LZMS install image — an ESD by content even when it is named install.wim (a
    // "lite" ISO, X-Lite and the like). It cannot be mounted either; it is converted to a plain WIM
    // first, like an ESD.
    [[nodiscard]] bool isSolidInstall() const;
    [[nodiscard]] bool isPackedSource() const { return isEsdSource() || isSolidInstall(); } // read for its editions, not mounted / edited
    [[nodiscard]] std::optional<int> failedIndex() const noexcept { return m_failedIndex; }

    void mount(int index);
    void unmount(bool commit);
    // ---- D-064: images mounted by other tools, in any folder ----------------------------------
    // Every DISM mount of this PC into AppState::systemMounts (elevated only; DISM needs it).
    void scanSystemMounts();
    // Work on the image mounted in `folder` (a remount first when DISM asks for one): its source is
    // opened and it becomes the mounted image, as at startup. Refused while something is mounted.
    void adoptMount(const std::filesystem::path& folder);
    // Unmount `folder` without saving (another tool's mount that is broken or not wanted).
    void discardMount(const std::filesystem::path& folder, std::wstring edition);
    void exportIndex(int index, const std::filesystem::path& destination);
    // Several editions, in this order, into one new WIM.
    void exportEditions(std::vector<int> indexes, const std::filesystem::path& destination);
    // Name and description of an edition, as DISM and Setup's edition list show them
    // (core::setImageText). An ISO is copied to the work folder first.
    void renameEdition(int index, std::wstring name, std::wstring description);
    // The edition of the mounted image and what it can be upgraded to (core::readEditions: two
    // dism.exe runs, several seconds — read once per mount, then answered at once). `done` runs on
    // the UI thread; a failure goes through Events::failed instead.
    void readEditions(std::function<void(const core::ImageEditions&)> done);
    // Takes `editions` as what readEditions found for the image mounted now (tests, render demos).
    void rememberEditions(core::ImageEditions editions);
    // Reads every stream of the install image and checks its SHA-1 (core::verifyWim); nothing is
    // written, an ISO is read in place. The result arrives through Events::verified.
    void verify();
    void convertEsd(const std::filesystem::path& destination);

    // ---- D-058 tools (Araçlar menu, row menu, Kaynak) ---------------------------------------------
    [[nodiscard]] bool isSwmSource() const;
    // Why the image file cannot be read / rewritten by a tool now (no source, busy, mounted).
    [[nodiscard]] std::optional<Str> toolsRefusal() const;
    // Every edition rewritten with `target` (Lzms = ESD); the source follows the new file.
    void recompress(core::WimCompression target);
    // install.swm, install2.swm … of at most `partMiB` each, next to `firstPart`. Not for an ESD.
    void splitSwm(const std::filesystem::path& firstPart, std::uint64_t partMiB);
    // A split source joined into one LZX WIM, which becomes the source.
    void mergeSwm(const std::filesystem::path& destination);
    // A copy of edition `index` at the end of the same WIM, named `name`.
    void duplicateEdition(int index, std::wstring name);
    // Edition `index` one place up (-1) or down (+1): Setup lists the editions in file order (AIO,
    // D-077). The WIM is rewritten (core::reorderImages); same conditions as deleting.
    void moveEdition(int index, int delta);
    // Editions of another ISO / WIM / ESD / SWM added to this one.
    void appendFrom(const std::filesystem::path& other, std::vector<int> indexes);
    // AIO (D-077): the setup files of the source's folder taken from Windows 10 media (an ISO, a
    // Media Creation Tool ESD or a setup folder) — core::replaceSetupMedia; the install image
    // stays. An ISO source is first copied to the work folder.
    void replaceSetupMedia(const std::filesystem::path& from);
    // A folder captured into `wim` (new or appended), which becomes the source. Needs elevation.
    void capture(const std::filesystem::path& folder, const std::filesystem::path& wim, core::ImageText text,
                 core::WimCompression compression);
    // Removes these editions; the WIM is rewritten with the ones that stay (core::removeImages),
    // which are renumbered. An ISO is copied to the work folder first. `label` is what the strip
    // and the toast call them: the edition's name, or "5 sürüm".
    void removeEditions(std::vector<int> indexes, std::wstring label);
    // Repairs the WinLove mount folder whatever its state (MountHealth: remount / discard /
    // clear leftovers, Explorer windows moved away first) plus DISM's own mount point cleanup.
    void cleanupMounts();
    // Inspects the mount folder on the engine and publishes it as AppState::mountFolder.
    void inspectMountFolder();
    void cancel();
    // Elevated start: restore the WinLove mount left from a previous run (remounts it if DISM
    // says "needs remount", discards it if invalid) and report it through Events::restored.
    void adoptExistingMount();

private:
    using Work = std::function<Result<void>(const core::TaskContext&)>;
    // Runs `work` as EngineOperation `op`; `onSuccess` runs on the UI thread.
    void run(EngineOperation op, Work work, std::function<void()> onSuccess, Failure failure);
    // For ISO sources: extracts to the work folder, switches the source to it, then `next`.
    void withWritableSource(int index, std::function<void()> next);
    void prepareWorkCopy(const std::filesystem::path& iso, const std::filesystem::path& folder, int index, bool extract,
                         bool fresh, std::function<void()> next);
    [[nodiscard]] std::optional<std::filesystem::path> installWimPath() const;
    [[nodiscard]] std::wstring editionName(int index) const;

    AppState& m_state;
    Events m_events;
    std::optional<int> m_failedIndex;
    // readEditions' answer for the image mounted then (the queue does not change it; Uygula unmounts).
    struct KnownEditions {
        std::filesystem::path mountDir;
        std::filesystem::path imagePath;
        int index = 0;
        core::ImageEditions editions;
    };
    std::optional<KnownEditions> m_editions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
