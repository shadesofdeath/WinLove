#include "core/uup/UupConvert.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/ImageFiles.h"
#include "core/image/MediaRefresh.h"
#include "core/image/RegistryEdit.h"
#include "core/image/dism/Dism.h"
#include "core/image/dism/DismExe.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/wim/WimGapi.h"
#include "core/io/ByteSource.h"
#include "core/iso/IsoBuilder.h"
#include "core/system/Process.h"
#include "core/uup/UupCatalog.h"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <regex>
#include <set>

namespace wl::core::uup {

namespace {

// The files Windows Setup needs inside its own image (boot.wim index 2, X:\sources\): the same
// names as Microsoft's media carry there; the ones the setup media has are copied in.
constexpr std::array kBootSources{
    L"alert.gif", L"api-ms-win-core-apiquery-l1-1-0.dll", L"api-ms-win-downlevel-advapi32-l1-1-0.dll",
    L"api-ms-win-downlevel-advapi32-l1-1-1.dll", L"api-ms-win-downlevel-advapi32-l2-1-0.dll",
    L"api-ms-win-downlevel-advapi32-l2-1-1.dll", L"api-ms-win-downlevel-advapi32-l3-1-0.dll",
    L"api-ms-win-downlevel-advapi32-l4-1-0.dll", L"api-ms-win-downlevel-kernel32-l1-1-0.dll",
    L"api-ms-win-downlevel-kernel32-l2-1-0.dll", L"api-ms-win-downlevel-ole32-l1-1-0.dll",
    L"api-ms-win-downlevel-ole32-l1-1-1.dll", L"api-ms-win-downlevel-shlwapi-l1-1-0.dll",
    L"api-ms-win-downlevel-shlwapi-l1-1-1.dll", L"api-ms-win-downlevel-user32-l1-1-0.dll",
    L"api-ms-win-downlevel-user32-l1-1-1.dll", L"api-ms-win-downlevel-version-l1-1-0.dll", L"appcompat.xsl",
    L"appcompat_bidi.xsl", L"appcompat_detailed_bidi_txt.xsl", L"appcompat_detailed_txt.xsl", L"appraiser.dll",
    L"ARUNIMG.dll", L"arunres.dll", L"autorun.dll", L"bcd.dll", L"bootsvc.dll", L"cmisetup.dll", L"compatctrl.dll",
    L"compatprovider.dll", L"compliance.ini", L"cryptosetup.dll", L"diager.dll", L"diagnostic.dll", L"diagtrack.dll",
    L"diagtrackrunner.exe", L"dism.exe", L"dismapi.dll", L"dismcore.dll", L"dismcoreps.dll", L"dismprov.dll",
    L"ext-ms-win-advapi32-encryptedfile-l1-1-0.dll", L"folderprovider.dll", L"hwcompat.dll", L"hwcompat.txt",
    L"hwcompatPE.txt", L"hwexclude.txt", L"hwexcludePE.txt", L"hwreqchk.dll", L"idwbinfo.txt", L"imagelib.dll",
    L"imagingprovider.dll", L"input.dll", L"lang.ini", L"locale.nls", L"logprovider.dll", L"MediaSetupUIMgr.dll",
    L"ndiscompl.dll", L"nlsbres.dll", L"ntdsupg.dll", L"offline.xml", L"pnpibs.dll", L"reagent.admx", L"reagent.dll",
    L"reagent.xml", L"rollback.exe", L"schema.dat", L"segoeui.ttf", L"ServicingCommon.dll", L"setup.exe",
    L"setupcompat.dll", L"SetupCore.dll", L"SetupHost.exe", L"SetupMgr.dll", L"SetupPlatform.cfg",
    L"SetupPlatform.dll", L"SetupPlatform.exe", L"SetupPrep.exe", L"SmiEngine.dll", L"spflvrnt.dll", L"spprgrss.dll",
    L"spwizeng.dll", L"spwizimg.dll", L"spwizres.dll", L"sqmapi.dll", L"testplugin.dll", L"unattend.dll",
    L"unbcl.dll", L"upgloader.dll", L"upgrade_frmwrk.xml", L"utcapi.dll", L"uxlib.dll", L"uxlibres.dll",
    L"vhdprovider.dll", L"w32uiimg.dll", L"w32uires.dll", L"warning.gif", L"wdsclient.dll", L"wdsclientapi.dll",
    L"wdscommonlib.dll", L"wdscore.dll", L"wdscsl.dll", L"wdsimage.dll", L"wdstptc.dll", L"wdsutil.dll",
    L"wimgapi.dll", L"wimprovider.dll", L"win32ui.dll", L"WinDlp.dll", L"winsetup.dll", L"wpx.dll", L"xmllite.dll",
    L"deployprovider.dll", L"osimageprovider.dll", L"pnppropmig.dll", L"UnattendMgr.dll", L"UpdateCompression.dll",
    L"WinSetupBoot.hiv", L"WinSetupBoot.sys", L"WinSetupMon.hiv", L"WinSetupMon.sys",
};
// …and of its language folder (X:\sources\tr-tr\).
constexpr std::array kBootLanguageSources{
    L"appraiser.dll.mui", L"arunres.dll.mui", L"cmisetup.dll.mui", L"compatctrl.dll.mui", L"compatprovider.dll.mui",
    L"deployprovider.dll.mui", L"dism.exe.mui", L"dismapi.dll.mui", L"dismcore.dll.mui", L"dismprov.dll.mui",
    L"folderprovider.dll.mui", L"imagingprovider.dll.mui", L"input.dll.mui", L"logprovider.dll.mui",
    L"MediaSetupUIMgr.dll.mui", L"nlsbres.dll.mui", L"osimageprovider.dll.mui", L"pnpibs.dll.mui", L"reagent.adml",
    L"reagent.dll.mui", L"rollback.exe.mui", L"setup.exe.mui", L"setup_help_upgrade_or_custom.rtf",
    L"setupcompat.dll.mui", L"SetupCore.dll.mui", L"SetupMgr.dll.mui", L"setupplatform.exe.mui", L"SetupPrep.exe.mui",
    L"smiengine.dll.mui", L"spwizres.dll.mui", L"upgloader.dll.mui", L"uxlibres.dll.mui", L"vhdprovider.dll.mui",
    L"vofflps.rtf", L"vofflps_server.rtf", L"w32uires.dll.mui", L"wdsclient.dll.mui", L"wdsimage.dll.mui",
    L"wimgapi.dll.mui", L"wimprovider.dll.mui", L"WinDlp.dll.mui", L"winsetup.dll.mui",
};

// The checkpoint cumulative updates a later LCU builds on (24H2+): DISM takes them from the LCU's
// folder on its own; only the newest LCU is given to it.
constexpr std::array kCheckpointKbs{L"kb5043080", L"kb5068181", L"kb5122055"};

constexpr wchar_t kWinPeKey[] = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\WinPE";
constexpr wchar_t kCurrentVersionKey[] = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

// A task's share [from, to] of the whole.
TaskContext part(const TaskContext& task, double from, double to) {
    return TaskContext{task.cancel, [&task, from, to](double f, std::wstring_view stage) {
                           task.report(from + (to - from) * std::clamp(f, 0.0, 1.0), stage);
                       }};
}

// The same, its progress under one stage key for the whole share (what the page names).
TaskContext phase(const TaskContext& task, double from, double to, const wchar_t* key) {
    return TaskContext{task.cancel, [&task, from, to, key](double f, std::wstring_view) {
                           task.report(from + (to - from) * std::clamp(f, 0.0, 1.0), key);
                       }};
}

RegistryWrite stringValue(std::wstring key, std::wstring name, std::wstring_view value, std::uint32_t type) {
    RegistryWrite w;
    w.kind = RegistryWrite::Kind::Set;
    w.key = std::move(key);
    w.name = std::move(name);
    w.type = type;
    for (const wchar_t c : value) {
        w.data.push_back(static_cast<std::uint8_t>(c & 0xFF));
        w.data.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
    }
    w.data.push_back(0);
    w.data.push_back(0);
    return w;
}

RegistryWrite deleteValue(std::wstring key, std::wstring name) {
    RegistryWrite w;
    w.kind = RegistryWrite::Kind::DeleteValue;
    w.key = std::move(key);
    w.name = std::move(name);
    return w;
}

Result<void> expandCab(const std::filesystem::path& cab, const std::filesystem::path& folder, std::wstring_view files) {
    auto expand = systemTool(L"expand.exe");
    if (!expand) {
        return std::unexpected(expand.error());
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    auto run = runProcess(std::format(L"\"{}\" \"{}\" -F:{} \"{}\"", *expand, cab.wstring(), files, folder.wstring()),
                          [](std::string_view) {});
    if (!run) {
        return std::unexpected(run.error());
    }
    if (*run != 0) {
        return fail(ErrorCode::IoError, std::format(L"expand.exe could not unpack it (exit {})", *run), cab.wstring());
    }
    return {};
}

// expand.exe -D: the names in a cabinet, one per line (lower-case).
std::wstring cabFileList(const std::filesystem::path& cab) {
    auto expand = systemTool(L"expand.exe");
    if (!expand) {
        return {};
    }
    std::string out;
    auto run = runProcess(std::format(L"\"{}\" -D \"{}\"", *expand, cab.wstring()), [&](std::string_view text) {
        if (out.size() < (4u << 20)) {
            out.append(text);
        }
    });
    if (!run) {
        return {};
    }
    return text::lower(utf8::decodeText(out));
}

std::wstring readText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return utf8::decodeText(bytes);
}

Result<void> copyInto(const std::filesystem::path& mountDir, const std::wstring& relative, const std::filesystem::path& source) {
    std::error_code ec;
    if (relative.find(L'\\') == std::wstring::npos) {
        // The image root (X:\setup.exe): the privileged writer refuses a path that short (its
        // rule for paths from presets); the root of a Windows PE image is the administrators' anyway.
        std::filesystem::copy_file(source, mountDir / relative, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail(ErrorCode::IoError, L"cannot copy into the image root", (mountDir / relative).wstring(), ec.value());
        }
        return {};
    }
    const auto folder = (mountDir / relative).parent_path();
    if (!std::filesystem::exists(folder, ec)) {
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            return fail(ErrorCode::IoError, L"cannot create the folder in the image", folder.wstring(), ec.value());
        }
    }
    return replaceImageFileFrom(mountDir, relative, source, FILE_ATTRIBUTE_NORMAL, TaskContext{});
}

// The background Setup and Windows PE show: the first the media has.
std::filesystem::path backgroundPicture(const std::filesystem::path& media) {
    for (const wchar_t* name : {L"background_cli.bmp", L"background_svr.bmp", L"background_cli.png", L"background_svr.png", L"winpe.jpg"}) {
        const auto p = media / L"sources" / name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            return p;
        }
    }
    return {};
}

Result<ImageInfo> metadataImage(const std::filesystem::path& esd, int index) {
    auto file = DiskFile::open(nativePath(esd));
    if (!file) {
        return std::unexpected(file.error());
    }
    auto wim = readWim(**file);
    if (!wim) {
        return std::unexpected(wim.error());
    }
    for (const auto& image : wim->images) {
        if (image.index == index) {
            return image;
        }
    }
    return fail(ErrorCode::NotFound, std::format(L"image {} is not in it", index), esd.wstring());
}

std::wstring versionOf(const std::filesystem::path& wim, int index) {
    auto image = metadataImage(wim, index);
    return image ? image->versionString() : std::wstring();
}

} // namespace

// ---- scanning ------------------------------------------------------------------------------

Result<UupSetFiles> scanUupFolder(const std::filesystem::path& folderInput) {
    const std::filesystem::path folder = nativePath(folderInput);
    UupSetFiles set;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const auto& path = entry.path();
        const std::wstring name = path.filename().wstring();
        const std::wstring lower = text::lower(name);
        switch (fileKind(name)) {
        case FileKind::Metadata: {
            auto image = metadataImage(path, 3);
            if (!image) {
                log::warn("uup", L"not a metadata ESD: " + describe(image.error()));
                continue;
            }
            UupEdition e;
            e.metadata = path;
            e.name = image->name;
            e.description = image->description;
            e.editionId = image->editionId;
            e.language = image->defaultLanguage.empty() && !image->languages.empty() ? image->languages.front()
                                                                                      : image->defaultLanguage;
            e.architecture = architectureName(image->architecture);
            e.build = image->build;
            e.revision = image->spBuild;
            set.editions.push_back(std::move(e));
            break;
        }
        case FileKind::PackageEsd: set.packageEsds.push_back(path); break;
        case FileKind::Edge: set.edge = path; break;
        case FileKind::Msu:
            if (lower.starts_with(L"windows1") && lower.find(L"-kb") != std::wstring::npos) {
                set.updates.push_back(path);
            }
            break;
        case FileKind::Cab:
            if (lower.starts_with(L"windows1") && lower.find(L"-kb") != std::wstring::npos) {
                set.updates.push_back(path);
            } else if (lower.find(L"aggregatedmetadata") == std::wstring::npos && lower != L"desktopdeployment.cab") {
                set.featureCabs.push_back(path);
            }
            break;
        default: break;
        }
    }
    if (set.editions.empty()) {
        return fail(ErrorCode::NotFound, L"no edition metadata (edition_language.esd) in the folder", folder.wstring());
    }
    // Pro before Home before the rest, like Microsoft's media lists them.
    std::ranges::sort(set.editions, [](const UupEdition& a, const UupEdition& b) {
        auto rank = [](const std::wstring& id) {
            const std::wstring l = text::lower(id);
            return l == L"professional" ? 0 : l == L"core" ? 1 : 2;
        };
        return std::pair(rank(a.editionId), a.editionId) < std::pair(rank(b.editionId), b.editionId);
    });
    std::ranges::sort(set.packageEsds);
    std::ranges::sort(set.featureCabs);
    std::ranges::sort(set.updates);
    return set;
}

UpdateRole updateRoleFrom(std::wstring_view fileNameView, bool hasMum, std::wstring_view mumView, std::wstring_view listView) {
    const std::wstring fileName = text::lower(fileNameView);
    const std::wstring mum = text::lower(mumView);
    const std::wstring list = text::lower(listView);
    for (const wchar_t* kb : kCheckpointKbs) {
        if (fileName.find(kb) != std::wstring::npos && fileName.ends_with(L".msu")) {
            return UpdateRole::Checkpoint;
        }
    }
    if (fileName.ends_with(L".msu")) {
        return fileName.find(L"ndp") != std::wstring::npos ? UpdateRole::DotNet : UpdateRole::Cumulative;
    }
    if (!hasMum) {
        return UpdateRole::SetupDu; // Setup's own files, no package
    }
    if (mum.find(L"package_for_rollupfix") != std::wstring::npos) {
        return UpdateRole::Cumulative;
    }
    if (mum.find(L"package_for_safeosdu") != std::wstring::npos ||
        (mum.find(L"winpe") != std::wstring::npos && mum.find(L"edition\"") == std::wstring::npos) ||
        list.find(L"winpe_tools") != std::wstring::npos || list.find(L"winre-tools") != std::wstring::npos) {
        return UpdateRole::SafeOs;
    }
    // Every update.mum names a servicing stack; only the files of the package tell one apart.
    if (list.find(L"enablement-package") != std::wstring::npos) {
        return UpdateRole::Enablement;
    }
    if (fileName.find(L"ndp") != std::wstring::npos || list.find(L"_netfx4") != std::wstring::npos) {
        return UpdateRole::DotNet;
    }
    if (list.find(L"-servicingstack_") != std::wstring::npos) {
        return UpdateRole::ServicingStack;
    }
    return UpdateRole::Other;
}

UpdateRole updateRole(const std::filesystem::path& file) {
    const std::wstring name = file.filename().wstring();
    if (text::lower(name).ends_with(L".msu")) {
        return updateRoleFrom(name, true, {}, {});
    }
    const std::wstring list = cabFileList(file);
    const bool hasMum = list.find(L"update.mum") != std::wstring::npos;
    std::wstring mum;
    if (hasMum) {
        const auto temp = std::filesystem::temp_directory_path() / std::format(L"wl-mum-{}", GetCurrentProcessId());
        if (expandCab(file, temp, L"update.mum")) {
            mum = readText(temp / L"update.mum");
        }
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
    }
    return updateRoleFrom(name, hasMum, mum, list);
}

std::wstring isoLabel(const UupSetFiles& set) {
    const UupEdition& first = set.editions.front();
    const std::wstring arch = first.architecture == L"arm64" ? L"A64" : first.architecture == L"x86" ? L"X86" : L"X64";
    std::wstring prefix = L"CCSA";
    if (set.editions.size() == 1) {
        const std::wstring id = text::lower(first.editionId);
        prefix = id == L"professional" ? L"CPRA" : id == L"core" ? L"CCRA" : id == L"education" ? L"CEDA" : L"CCSA";
    }
    return std::format(L"{}_{}FRE_{}_DV9", prefix, arch, text::upper(first.language));
}

// ---- conversion ----------------------------------------------------------------------------

namespace {

struct Context {
    Dism& dism;
    const ConvertOptions& options;
    UupSetFiles set;
    std::filesystem::path work, media, refs, mount, winre, saved;
    std::vector<std::filesystem::path> references;
    ConvertResult result;
};

// 1. The feature-on-demand cabinets as reference WIMs.
Result<void> prepareReferences(Context& c, const TaskContext& task) {
    c.references = c.set.packageEsds;
    std::error_code ec;
    std::filesystem::create_directories(c.refs, ec);
    const double n = static_cast<double>(std::max<std::size_t>(c.set.featureCabs.size(), 1));
    for (std::size_t i = 0; i < c.set.featureCabs.size(); ++i) {
        if (auto go = task.cancel.check(L"references"); !go) {
            return go;
        }
        const auto& cab = c.set.featureCabs[i];
        const auto stem = cab.stem().wstring();
        const auto wim = c.refs / (stem + L".wim");
        task.report(static_cast<double>(i) / n, L"references");
        if (std::filesystem::exists(wim, ec)) { // a second run
            c.references.push_back(wim);
            continue;
        }
        const auto dir = c.work / L"cab" / stem;
        std::filesystem::remove_all(dir, ec);
        if (auto x = expandCab(cab, dir, L"*"); !x) {
            return x;
        }
        if (std::filesystem::exists(dir / L"update.mum", ec)) {
            if (auto r = captureReference(dir, wim, TaskContext{task.cancel, {}}); !r) {
                return r;
            }
            c.references.push_back(wim);
        }
        std::filesystem::remove_all(dir, ec);
    }
    log::info("uup", std::format(L"{} reference file(s)", c.references.size()));
    return {};
}

// 2. Setup media from index 1.
Result<void> buildMedia(Context& c, const TaskContext& task) {
    std::error_code ec;
    std::filesystem::remove_all(c.media, ec);
    if (auto r = applyImage(c.set.editions.front().metadata, 1, c.media, task); !r) {
        return r;
    }
    std::filesystem::remove(c.media / L"MediaMeta.xml", ec);
    return {};
}

// 3. install.wim: every edition's image 3 with the references.
Result<void> exportEditions(Context& c, const TaskContext& task) {
    const auto install = c.media / L"sources" / L"install.wim";
    std::error_code ec;
    std::filesystem::remove(install, ec);
    const double n = static_cast<double>(c.set.editions.size());
    for (std::size_t i = 0; i < c.set.editions.size(); ++i) {
        const auto& e = c.set.editions[i];
        if (auto r = exportImageWithReferences(e.metadata, 3, c.references, install, WimCompression::Lzx,
                                               part(task, static_cast<double>(i) / n, static_cast<double>(i + 1) / n));
            !r) {
            return r;
        }
        if (auto r = setImageText(install, static_cast<int>(i) + 1, ImageText{e.name, e.description, e.editionId}); !r) {
            return r;
        }
        c.result.editions.push_back(e.name);
    }
    return {};
}

// 4. WinRE on its own.
Result<void> exportWinre(Context& c, const TaskContext& task) {
    std::error_code ec;
    std::filesystem::remove(c.winre, ec);
    if (auto r = exportImage(c.set.editions.front().metadata, 2, c.winre, WimCompression::Lzx, task); !r) {
        return r;
    }
    return setBootImage(c.winre, 1);
}

struct UpdatePlan {
    std::filesystem::path ssu;
    std::vector<std::filesystem::path> packages; // enablement, .NET, other — before the LCU
    std::filesystem::path lcu;                   // the newest cumulative update (its checkpoint next to it)
    std::filesystem::path setupDu;
    std::filesystem::path safeOs;
};

UpdatePlan planUpdates(const Context& c) {
    UpdatePlan plan;
    std::uintmax_t lcuSize = 0;
    for (const auto& file : c.set.updates) {
        const UpdateRole role = updateRole(file);
        std::error_code ec;
        log::info("uup", std::format(L"update {}: role {}", file.filename().wstring(), static_cast<int>(role)));
        switch (role) {
        case UpdateRole::ServicingStack: plan.ssu = file; break;
        case UpdateRole::Cumulative: {
            const auto size = std::filesystem::file_size(file, ec);
            if (plan.lcu.empty() || size > lcuSize) {
                plan.lcu = file;
                lcuSize = size;
            }
            break;
        }
        case UpdateRole::Checkpoint: break; // DISM takes it from the LCU's folder
        case UpdateRole::Enablement: plan.packages.insert(plan.packages.begin(), file); break;
        case UpdateRole::DotNet:
        case UpdateRole::Other: plan.packages.push_back(file); break;
        case UpdateRole::SetupDu: plan.setupDu = file; break;
        case UpdateRole::SafeOs: plan.safeOs = file; break;
        }
    }
    return plan;
}

// 5. Every edition mounted once: Edge, updates, WinRE; the first one gives the media its boot
// fonts, the boot manager (when updated) and Setup's xmllite.dll.
Result<void> serviceEditions(Context& c, const UpdatePlan& plan, const TaskContext& task) {
    const auto install = c.media / L"sources" / L"install.wim";
    const bool updating = c.options.updates && (!plan.lcu.empty() || !plan.packages.empty() || !plan.ssu.empty());
    const int count = static_cast<int>(c.set.editions.size());
    for (int i = 1; i <= count; ++i) {
        const double base = static_cast<double>(i - 1) / count;
        const double share = 1.0 / count;
        auto sub = [&](double from, double to) { return part(task, base + share * from, base + share * to); };
        if (auto m = mountSafely(c.dism, install, i, c.mount, /*readOnly=*/false, sub(0.0, 0.05)); !m) {
            return std::unexpected(m.error());
        }
        auto work = [&]() -> Result<void> {
            if (c.options.updates) {
                auto session = c.dism.openSession(c.mount);
                if (!session) {
                    return std::unexpected(session.error());
                }
                if (c.options.edge && !c.set.edge.empty()) {
                    task.report(base + share * 0.06, L"edge");
                    auto edge = runDismExe(**session, std::format(L"/Add-Edge /SupportPath:\"{}\"", c.set.edge.parent_path().wstring()));
                    if (!edge || edge->exitCode != 0) {
                        const std::wstring why = edge ? dismExeFailure(*edge, L"Edge").message : describe(edge.error());
                        log::warn("uup", L"Edge not added: " + why);
                        c.result.warnings.push_back(L"Edge: " + why);
                    }
                }
                if (!plan.ssu.empty()) {
                    if (auto r = addPackageOrDismExe(**session, plan.ssu, sub(0.1, 0.15)); !r) {
                        return r;
                    }
                }
                for (std::size_t k = 0; k < plan.packages.size(); ++k) {
                    const auto& p = plan.packages[k];
                    if (auto r = addPackageOrDismExe(**session, p, sub(0.15, 0.25)); !r) {
                        // One package that does not apply (an enablement for another edition) must
                        // not cost the image.
                        log::warn("uup", std::format(L"{} not added: {}", p.filename().wstring(), describe(r.error())));
                        c.result.warnings.push_back(p.filename().wstring() + L": " + r.error().message);
                    }
                }
                if (!plan.lcu.empty()) {
                    auto r = addPackageOrDismExe(**session, plan.lcu, sub(0.25, 0.85));
                    if (!r && r.error().hresult == static_cast<std::int32_t>(0x8007007E)) {
                        r = addPackageOrDismExe(**session, plan.lcu, sub(0.25, 0.85)); // known first-pass stop
                    }
                    if (!r) {
                        return r;
                    }
                }
                if (updating) {
                    auto cleaned = runDismExe(**session, L"/Cleanup-Image /StartComponentCleanup",
                                              [&](double f) { task.report(base + share * (0.85 + 0.05 * f), L"cleanup"); });
                    if (!cleaned || cleaned->exitCode != 0) {
                        log::warn("uup", L"component cleanup did not run");
                    }
                }
            }
            if (auto r = replaceImageFileFrom(c.mount, L"Windows\\System32\\Recovery\\Winre.wim", c.winre,
                                              FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, TaskContext{task.cancel, {}});
                !r) {
                return r;
            }
            if (i == 1) {
                std::error_code ec;
                std::filesystem::create_directories(c.saved, ec);
                std::filesystem::copy_file(c.mount / L"Windows" / L"System32" / L"xmllite.dll", c.saved / L"xmllite.dll",
                                           std::filesystem::copy_options::overwrite_existing, ec);
                const auto fonts = c.mount / L"Windows" / L"Boot" / L"Fonts";
                for (const auto& to : {c.media / L"boot" / L"fonts", c.media / L"efi" / L"microsoft" / L"boot" / L"fonts"}) {
                    std::filesystem::create_directories(to, ec);
                    std::filesystem::copy(fonts, to, std::filesystem::copy_options::overwrite_existing, ec);
                }
                if (updating) {
                    // The boot manager of the updated Windows (Microsoft, step 28).
                    const auto efi = c.mount / L"Windows" / L"Boot" / L"EFI";
                    const std::wstring bootx = c.set.editions.front().architecture == L"arm64" ? L"bootaa64.efi" : L"bootx64.efi";
                    std::filesystem::copy_file(efi / L"bootmgfw.efi", c.media / L"efi" / L"boot" / bootx,
                                               std::filesystem::copy_options::overwrite_existing, ec);
                    std::filesystem::copy_file(efi / L"bootmgr.efi", c.media / L"bootmgr.efi",
                                               std::filesystem::copy_options::overwrite_existing, ec);
                    if (std::filesystem::exists(efi / L"boot.stl", ec)) {
                        std::filesystem::copy_file(efi / L"boot.stl", c.media / L"efi" / L"microsoft" / L"boot" / L"boot.stl",
                                                   std::filesystem::copy_options::overwrite_existing, ec);
                    }
                    for (const wchar_t* dll : {L"ServicingCommon.dll", L"unbcl.dll"}) {
                        std::filesystem::copy_file(c.mount / L"Windows" / L"System32" / dll, c.saved / dll,
                                                   std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            }
            return {};
        }();
        const TaskContext unmountTask{CancelToken{}, [&](double f, std::wstring_view s) {
                                          task.report(base + share * (0.9 + 0.1 * f), s);
                                      }};
        if (!work) {
            log::error("uup", std::format(L"edition {}: {}; discarding", i, describe(work.error())));
            (void)unmountSafely(c.dism, c.mount, /*commit=*/false, unmountTask);
            return work;
        }
        if (auto u = unmountSafely(c.dism, c.mount, /*commit=*/true, unmountTask); !u) {
            return std::unexpected(u.error());
        }
    }
    c.result.version = versionOf(install, 1);
    return {};
}

// The Setup dynamic update over the media (files it has, newer; its language folders; D-080).
Result<void> refreshMedia(Context& c, const UpdatePlan& plan, const TaskContext& task) {
    if (plan.setupDu.empty()) {
        return {};
    }
    const auto du = c.work / L"setupdu";
    if (auto x = expandSetupDynamicUpdate(plan.setupDu, du, task); !x) {
        return std::unexpected(x.error());
    }
    std::error_code ec;
    for (const auto& f : mediaRefreshFiles(c.media, du, {})) {
        const auto target = c.media / f.path;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::copy_file(f.file, target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail(ErrorCode::IoError, L"cannot update the media file", target.wstring(), ec.value());
        }
    }
    // Setup of 24H2+ loads these from the serviced Windows' build (the dynamic update has none).
    for (const wchar_t* dll : {L"ServicingCommon.dll", L"unbcl.dll"}) {
        if (std::filesystem::exists(c.saved / dll, ec)) {
            std::filesystem::copy_file(c.saved / dll, c.media / L"sources" / dll, std::filesystem::copy_options::overwrite_existing, ec);
        }
    }
    std::filesystem::remove_all(du, ec);
    return {};
}

// 6. boot.wim from WinRE.
Result<void> buildBootWim(Context& c, const TaskContext& task) {
    const auto boot = c.media / L"sources" / L"boot.wim";
    const auto work = c.work / L"boot.wim";
    std::error_code ec;
    std::filesystem::remove(work, ec);
    std::filesystem::copy_file(c.winre, work, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot copy WinRE", work.wstring(), ec.value());
    }
    const std::wstring arch = c.set.editions.front().architecture;
    const std::filesystem::path background = backgroundPicture(c.media);
    const std::wstring language = text::lower(c.set.editions.front().language);

    // Index 1: Windows PE.
    if (auto r = setImageText(work, 1, ImageText{std::format(L"Microsoft Windows PE ({})", arch),
                                                 std::format(L"Microsoft Windows PE ({})", arch), L"9"});
        !r) {
        return r;
    }
    auto edit = [&](int index, bool setup, const TaskContext& sub) -> Result<void> {
        if (auto m = mountSafely(c.dism, work, index, c.mount, false, part(sub, 0.0, 0.3)); !m) {
            return std::unexpected(m.error());
        }
        auto changed = [&]() -> Result<void> {
            if (auto r = unlinkImageFile(c.mount, L"Windows\\System32\\winpeshl.ini"); !r) {
                return r;
            }
            {
                OfflineRegistry registry(c.mount);
                std::vector<RegistryWrite> writes;
                if (!setup) {
                    writes.push_back(stringValue(kCurrentVersionKey, L"SystemRoot", L"X:\\$windows.~bt\\Windows", REG_SZ));
                    writes.push_back(stringValue(kWinPeKey, L"InstRoot", L"X:\\$windows.~bt\\", REG_SZ));
                }
                writes.push_back(stringValue(kWinPeKey, L"CustomBackground",
                                             setup ? L"%SystemRoot%\\System32\\setup.bmp" : L"%SystemRoot%\\System32\\winre.jpg",
                                             REG_EXPAND_SZ));
                writes.push_back(deleteValue(kWinPeKey, L"CustomShell"));
                for (const auto& w : writes) {
                    if (auto r = registry.apply(w); !r) {
                        return r;
                    }
                }
            }
            if (!background.empty()) {
                std::vector<std::wstring> targets{L"Windows\\System32\\winpe.jpg"};
                if (!std::filesystem::exists(c.mount / L"Windows" / L"System32" / L"winre.jpg", ec)) {
                    targets.push_back(L"Windows\\System32\\winre.jpg");
                }
                if (setup) {
                    targets.push_back(L"sources\\background.bmp");
                    targets.push_back(L"Windows\\System32\\setup.bmp");
                }
                for (const auto& t : targets) {
                    if (auto r = copyInto(c.mount, t, background); !r) {
                        return r;
                    }
                }
            }
            if (setup) {
                if (auto r = copyInto(c.mount, L"setup.exe", c.media / L"setup.exe"); !r) {
                    return r;
                }
                if (std::filesystem::exists(c.media / L"sources" / L"inf" / L"setup.cfg", ec)) {
                    if (auto r = copyInto(c.mount, L"sources\\inf\\setup.cfg", c.media / L"sources" / L"inf" / L"setup.cfg"); !r) {
                        return r;
                    }
                }
                for (const wchar_t* name : kBootSources) {
                    std::filesystem::path from = c.media / L"sources" / name;
                    if (!std::filesystem::exists(from, ec) && std::wstring_view(name) == L"xmllite.dll") {
                        from = c.saved / name;
                    }
                    if (std::filesystem::is_regular_file(from, ec)) {
                        if (auto r = copyInto(c.mount, std::wstring(L"sources\\") + name, from); !r) {
                            return r;
                        }
                    }
                }
                for (const wchar_t* name : kBootLanguageSources) {
                    const auto from = c.media / L"sources" / language / name;
                    if (std::filesystem::is_regular_file(from, ec)) {
                        if (auto r = copyInto(c.mount, L"sources\\" + language + L"\\" + name, from); !r) {
                            return r;
                        }
                    }
                }
            }
            return {};
        }();
        const TaskContext unmountTask{CancelToken{}, [&](double f, std::wstring_view s) { sub.report(0.7 + 0.3 * f, s); }};
        if (!changed) {
            (void)unmountSafely(c.dism, c.mount, false, unmountTask);
            return changed;
        }
        if (auto u = unmountSafely(c.dism, c.mount, true, unmountTask); !u) {
            return std::unexpected(u.error());
        }
        return {};
    };
    if (auto r = edit(1, false, part(task, 0.0, 0.4)); !r) {
        return r;
    }
    // Index 2: Windows Setup, a fresh copy of WinRE; the one the media boots.
    if (auto r = exportImage(c.winre, 1, work, WimCompression::Lzx, part(task, 0.4, 0.5)); !r) {
        return r;
    }
    if (auto r = setImageText(work, 2, ImageText{std::format(L"Microsoft Windows Setup ({})", arch),
                                                 std::format(L"Microsoft Windows Setup ({})", arch), L"2"});
        !r) {
        return r;
    }
    if (auto r = edit(2, true, part(task, 0.5, 0.9)); !r) {
        return r;
    }
    // A commit only appends: both images into a fresh file, image 2 the boot one.
    std::filesystem::remove(boot, ec);
    const std::array<int, 2> both{1, 2};
    if (auto r = exportImages(work, both, boot, WimCompression::Lzx, part(task, 0.9, 1.0)); !r) {
        return r;
    }
    std::filesystem::remove(work, ec);
    return setBootImage(boot, 2);
}

} // namespace

Result<ConvertResult> convertUup(Dism& dism, const ConvertOptions& options, const TaskContext& task) {
    auto scanned = scanUupFolder(options.uupFolder);
    if (!scanned) {
        return std::unexpected(scanned.error());
    }
    Context c{dism, options, std::move(*scanned)};
    c.work = nativePath(options.workFolder);
    c.media = nativePath(options.mediaFolder);
    c.refs = c.work / L"refs";
    c.mount = c.work / L"mount";
    c.winre = c.work / L"winre.wim";
    c.saved = c.work / L"saved";
    c.result.label = isoLabel(c.set);
    std::error_code ec;
    std::filesystem::create_directories(c.work, ec);
    log::info("uup", std::format(L"convert {}: {} edition(s), {} package ESD(s), {} feature cab(s), {} update(s)",
                                 options.uupFolder.wstring(), c.set.editions.size(), c.set.packageEsds.size(),
                                 c.set.featureCabs.size(), c.set.updates.size()));

    const bool updates = options.updates && !c.set.updates.empty();
    // Shares of the whole: with updates the mounts take most of the time.
    const double s1 = 0.05, s2 = 0.08, s3 = updates ? 0.25 : 0.45, s4 = updates ? 0.27 : 0.5, s5 = updates ? 0.82 : 0.65,
                 s6 = updates ? 0.9 : 0.82, s7 = updates ? 0.93 : 0.88;
    if (auto r = prepareReferences(c, phase(task, 0.0, s1, L"references")); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = buildMedia(c, phase(task, s1, s2, L"media")); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = exportEditions(c, phase(task, s2, s3, L"export")); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = exportWinre(c, phase(task, s3, s4, L"export")); !r) {
        return std::unexpected(r.error());
    }
    const UpdatePlan plan = updates ? planUpdates(c) : UpdatePlan{};
    if (auto r = serviceEditions(c, plan, phase(task, s4, s5, updates ? L"updates" : L"service")); !r) {
        return std::unexpected(r.error());
    }
    if (updates) {
        if (auto r = refreshMedia(c, plan, phase(task, s5, s5 + 0.01, L"media")); !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = buildBootWim(c, phase(task, s5 + 0.01, s6, L"boot")); !r) {
        return std::unexpected(r.error());
    }
    // install.wim without what the commits left behind; an ESD when asked.
    const auto install = c.media / L"sources" / L"install.wim";
    if (options.compression == WimCompression::Lzms) {
        if (auto r = recompressWim(install, WimCompression::Lzms, phase(task, s6, s7, L"optimize")); !r) {
            return std::unexpected(r.error());
        }
    } else if (auto r = optimizeWim(install, phase(task, s6, s7, L"optimize")); !r) {
        return std::unexpected(r.error());
    }
    if (!options.output.empty()) {
        IsoOptions iso;
        iso.sourceFolder = c.media;
        iso.output = nativePath(options.output);
        iso.volumeLabel = c.result.label;
        iso.boot = BootMode::UefiAndBios;
        auto built = buildIso(iso, phase(task, s7, 1.0, L"iso"));
        if (!built) {
            return std::unexpected(built.error());
        }
        c.result.iso = iso.output;
        c.result.bytes = built->bytes;
        std::filesystem::remove_all(c.media, ec);
    } else {
        c.result.media = c.media;
    }
    std::filesystem::remove_all(c.work, ec);
    task.report(1.0, L"done");
    log::info("uup", std::format(L"converted: {} ({}), {}", c.result.iso.empty() ? c.media.wstring() : c.result.iso.wstring(),
                                 c.result.version, c.result.label));
    return c.result;
}

} // namespace wl::core::uup
