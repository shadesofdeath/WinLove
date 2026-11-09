#pragma once
// D-116: the drivers an image cannot boot without, whichever component a file belongs to.
//
// Report 2026-11-07: BitLocker's store glob (D-113, "microsoft-windows-securestartup*") took
// System32\drivers\fvevol.sys out of the image. Its service stayed (boot start, ErrorControl
// critical, a lower filter of every volume), and the installed system stopped in the boot loader
// with 0xc000000f before Windows could start. File removal (store copies, recipe paths) never takes:
//   - a kernel / file system driver that starts at boot or system start (Start 0 / 1), or sits in a
//     load order group of the boot path (Ntfs is Start 3 and the loader takes it by its group;
//     stornvme / storahci become boot start on the PC whose system disk they drive), unless the
//     same step removes its service as well (then nothing loads the file);
//   - a driver a device class names as an upper or lower filter (Control\Class\{…}), ever: the
//     class keeps the name, and a filter that does not load takes the whole device stack down
//     (a volume filter: 0x7B). Its service key stays too.
//
// D-117 (report 2026-11-08, the same image after D-116, the same 0xc000000f): the error reporting
// store glob ("microsoft-windows-errorreportingkernel") took System32\drivers\werkernel.sys, and
// ntoskrnl.exe imports it (through the API set ext-ms-win-ntos-werkernel): the loader could not
// load the kernel itself. What a boot module imports is as necessary as the module, so removal
// also keeps:
//   - the loader's own modules (ntoskrnl.exe, hal.dll, kdcom.dll, ci.dll, the microcode updates,
//     the API set schema …) and the code page files Control\Nls names;
//   - every module these and the kept boot drivers import, transitively, an API set resolved through
//     the image's own schema (Hyper-V's glob took winhv.sys / vmbkmcl.sys while vmbus.sys, a boot
//     driver, still imports them; Vid.sys could not find winhvr.sys);
//   - the same past the loader (the VM test of the repaired image): the session and sign-in programs,
//     the shell and Setup's own steps, and the binary of every service that starts by itself and
//     stays, with what they import (explorer.exe imports AEPIC.dll, the compatibility removal took it;
//     iertutil.dll imports srvcli.dll, the SMB server removal took it);
//   - what those programs load by name (winlogon.exe / wininit.exe: utcapi.dll — the telemetry
//     removal took it, the session's csrss ended a second after it started and the screen stayed
//     black on the boot spinner) and the modules Setup's sysprep steps run (ProvSysprep.dll — the
//     provisioning removal took it, specialize stopped: "Windows could not configure Windows").
#include "base/Result.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

// A service's ImagePath ("\SystemRoot\System32\drivers\fvevol.sys", "System32\DRIVERS\x.sys",
// "\??\C:\Windows\…", none → System32\drivers\<service>.sys) as a lower-case path below the image
// root: "windows\system32\drivers\fvevol.sys". Empty when it is outside Windows or not a plain path.
[[nodiscard]] std::wstring driverImageFile(std::wstring_view imagePath, std::wstring_view service);

// `path` (a file inside the mounted image) the same way: lower case, below `mountDir`; empty when outside.
[[nodiscard]] std::wstring imageRelativePath(const std::filesystem::path& path, const std::filesystem::path& mountDir);

// A file the loader may read: a module (.sys .dll .exe .efi) or a code page / language table (.nls).
[[nodiscard]] bool isBootFileType(const std::filesystem::path& path);
// The files of that kind a removal path is (a file) or holds (a folder, links not followed).
[[nodiscard]] std::vector<std::filesystem::path> bootFilesUnder(const std::filesystem::path& path);

// The DLL names a PE file imports (its import directory, in order, as written); empty when the file
// is missing or not a PE image.
[[nodiscard]] std::vector<std::wstring> peImports(const std::filesystem::path& file);
// The same for its delay-load directory: the DLLs it loads the first time it calls into them.
[[nodiscard]] std::vector<std::wstring> peDelayImports(const std::filesystem::path& file);

// An API set schema (Windows\System32\apisetschema.dll, namespace version 6: Windows 10 and later):
// contract without its last "-n" (lower case, "ext-ms-win-ntos-werkernel-l1-1") → every host it
// names (lower case, "werkernel.sys").
using ApiSetSchema = std::map<std::wstring, std::vector<std::wstring>>;
[[nodiscard]] Result<ApiSetSchema> readApiSetSchema(const std::filesystem::path& file);
// The files an imported name means: an API set's hosts ("api-…" / "ext-…", none when the schema has
// no host for it), the name itself otherwise; lower case.
[[nodiscard]] std::vector<std::wstring> importHosts(const ApiSetSchema& schema, std::wstring_view dll);

// The strings of a REG_MULTI_SZ value.
[[nodiscard]] std::vector<std::wstring> multiStrings(const std::vector<std::uint8_t>& data);

struct BootDriver {
    std::wstring service;   // the key's name ("Nls\CodePage\1254" for a code page file)
    std::wstring file;      // driverImageFile
    bool early = false;     // Start 0 / 1, a boot path group, a code page, an automatic Win32 service
    bool filter = false;    // named by a device class as a filter
};
// Every such driver in the image's SYSTEM hive (all control sets), the code page files the loader
// reads (Control\Nls\CodePage ACP / OEMCP) and the binary of every Win32 service under Windows\ that
// starts by itself (Start 2: its ServiceDll, or its program) as early entries, read through offreg:
// the hive must not be loaded (the caller lets DISM go first).
[[nodiscard]] Result<std::vector<BootDriver>> findBootDrivers(const std::filesystem::path& mountDir);

// The files removal keeps (file → service) when the same step removes `services` (names, any case):
// filters always, boot / system start drivers whose service stays.
[[nodiscard]] std::map<std::wstring, std::wstring> keptDriverFiles(const std::vector<BootDriver>& drivers,
                                                                   const std::vector<std::wstring>& services);
// Filter drivers among `services`: their keys stay (lower-case names).
[[nodiscard]] std::vector<std::wstring> keptFilterServices(const std::vector<BootDriver>& drivers,
                                                           const std::vector<std::wstring>& services);

// The loader's own files (below the image root, lower case): kept whatever a recipe names.
[[nodiscard]] const std::vector<std::wstring>& loaderFiles();
// The programs Windows runs to start a session, sign in and finish Setup (same form).
[[nodiscard]] const std::vector<std::wstring>& sessionPrograms();

// The module and program names a file holds as text (single-byte or UTF-16, "name.dll" /
// "name.exe" ended by NUL), lower case: what it may load or start by name. winlogon.exe and
// wininit.exe load utcapi.dll this way, and the sign-in session never starts without it (the
// telemetry removal took it); ProvSysprep.dll starts provtool.exe (the VM test, 2026-11-08).
[[nodiscard]] std::vector<std::wstring> namedModules(const std::filesystem::path& file);

// The modules Setup's own steps run (Windows\System32\Sysprep\ActionFiles\*.xml, moduleName,
// $(runtime.system32) / $(runtime.windows) resolved), below the image root, lower case: specialize
// stops when one is missing ("Could not load DLL …\ProvSysprep.dll", the provisioning removal).
[[nodiscard]] std::vector<std::wstring> sysprepModules(const std::filesystem::path& mountDir);

// Everything removal keeps so Windows starts, signs in and finishes Setup (file → why):
// keptDriverFiles, the loader's files, the session programs with the modules they load by name,
// Setup's sysprep modules, every host the image's API set schema names (srpapi.dll: without it
// PowerShell runs in Constrained Language), and every module these import, transitively (only files the image has;
// an import is looked up in System32, System32\drivers and the importer's folder). Fails when the
// image's API set schema cannot be read: the caller then keeps every boot file type.
[[nodiscard]] Result<std::map<std::wstring, std::wstring>> keptBootFiles(const std::filesystem::path& mountDir,
                                                                         const std::vector<BootDriver>& drivers,
                                                                         const std::vector<std::wstring>& services);

// D-118 (the VM test of the D-117 image, 2026-11-08): a module that stays loads what it imports,
// at start or the first time it calls into it (delay load). With the file gone the call returns
// E_NOTIMPL or the process stops: the app deployment service delay-loads IsoSessionCore.dll (the
// isolation session removal took it), shell32 / userenv / twinui cldapi.dll (Cloud Files),
// feclient.dll efscore.dll (EFS) — and every Store package, the taskbar's frameworks with them,
// failed to register for a new account, so OOBE started over and over ("Why did my PC restart?").
// Of `going` (lower-case paths below the image root, the files a step is about to remove), those a
// module that stays imports or delay-loads (file → why), transitively among `going`. Looked at:
// Windows, System32, System32\drivers and SysWOW64 (their own files) and everything under
// SystemApps and ImmersiveControlPanel; an import resolves through the image's API set schema to
// System32 (SysWOW64 for a 32-bit importer there), System32\drivers or the importer's folder.
// Fails when the schema cannot be read: the caller then keeps every module.
[[nodiscard]] Result<std::map<std::wstring, std::wstring>> keptImportedFiles(const std::filesystem::path& mountDir,
                                                                             const std::vector<std::wstring>& going);

// D-128: one apply removes up to a few hundred components, and each step read the same things again
// (26200, per step: the component index ~7 s, the boot analysis ~1.8 s, every module's imports
// ~1.4 s). While a scope is open, these readers keep what they read between steps as long as the
// files it came from are unchanged (size and last write: the hive DISM lets go of before a step, the
// module itself). Apply opens one for its run; a single call and the tests read afresh.
class ImageScanScope {
public:
    ImageScanScope();
    ~ImageScanScope();
    ImageScanScope(const ImageScanScope&) = delete;
    ImageScanScope& operator=(const ImageScanScope&) = delete;
};
// The open scope's number (each scope its own), 0 when none is open: what was kept under another
// number is read again.
[[nodiscard]] std::uint64_t imageScanGeneration();
// A file's size and last write time (zeros when it cannot be read: never taken as unchanged).
struct FileStamp {
    std::uint64_t size = 0;
    std::uint64_t written = 0;
    [[nodiscard]] bool operator==(const FileStamp&) const = default;
};
[[nodiscard]] FileStamp fileStamp(const std::filesystem::path& file);

} // namespace wl::core
