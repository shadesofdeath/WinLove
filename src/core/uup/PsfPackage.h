#pragma once
// A Windows 11 cumulative update as UUP and the 24H2+ .msu carry it — a container of manifests
// (Windows11.0-KB…-x64.wim) and its payload (the .psf beside it) — made into the folder CBS installs
// from (update.mum), as PSFExtractor does: the container's express.psf.cix.xml says, for every file,
// where its bytes are in the .psf and how they are stored (RAW, or a delta against nothing: PA19,
// PA30, PA31). DISM then adds that update.mum itself, without Windows Update's agent — the way in
// when the agent refuses the .msu offline (a hotpatch-capable LCU: "Hotpatching is not supported on
// this machine/image", 0x800F8012).
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core::uup {

// The checkpoint cumulative updates a later LCU builds on (24H2+), oldest first. DISM takes them
// from the LCU's folder on its own; only the newest LCU is given to it.
inline constexpr std::array<std::wstring_view, 3> kCheckpointKbs{L"kb5043080", L"kb5068181", L"kb5122055"};

// The names in a 24H2+ .msu — a WIM, not compressed; empty for an older, cabinet one.
[[nodiscard]] std::vector<std::wstring> msuNames(const std::filesystem::path& msu);
// A hotpatch package among them ("Windows11.0-KB…-Hotpatch-x64.cab").
[[nodiscard]] bool hotpatchInside(const std::vector<std::wstring>& names);
// The checkpoint .msu files in the folder of `msu` (where DISM looks for them), oldest first.
[[nodiscard]] std::vector<std::filesystem::path> checkpointsBeside(const std::filesystem::path& msu);

struct ExpandedMsu {
    std::filesystem::path package; // the folder CBS installs (its update.mum)
    std::filesystem::path ssu;     // the servicing stack cab it carried (empty: none)
};
// A 24H2+ .msu taken apart under `work`: unpacked (wimgapi, admin), its cumulative update
// (Windows1x.0-KB…-<arch>.wim + .psf, not the hotpatch's) expanded into <work>\<name>.x, its SSU cab
// kept in <work>\ssu\ and the rest removed (the .psf: gigabytes). Done once: <work>\<name>.done.
[[nodiscard]] Result<ExpandedMsu> expandMsu(const std::filesystem::path& msu, const std::filesystem::path& work,
                                            const TaskContext& task, const std::filesystem::path& fallbackDlls = {});

struct PsfEntry {
    enum class Kind : std::uint8_t { Raw, Pa19, Pa30, Pa31 };
    std::wstring name;        // its path in the package folder ("amd64_…\f\ntdll.dll")
    std::uint64_t offset = 0; // its bytes in the .psf
    std::uint32_t length = 0;
    Kind kind = Kind::Raw;
    std::uint64_t time = 0;   // last write time (FILETIME ticks; 0 = none given)
    std::string sha256;       // the written file's, upper-case hex (empty = not checked)
};

// express.psf.cix.xml → its files (Container/Files/File[name,time]/Delta/Source[type,offset,length]).
// ParseError when it is not one; a name that would leave the folder (.., a drive) is refused too.
[[nodiscard]] Result<std::vector<PsfEntry>> parsePsfIndex(std::string_view xml);

// The kind its type attribute names; an unknown type by the delta's own signature (the payload's
// first bytes "PA19" / "PA30" / "PA31"), else RAW.
[[nodiscard]] PsfEntry::Kind psfKind(std::string_view type, std::string_view firstBytes) noexcept;

// Every entry written under `folder` from `psf`, each checked against its SHA-256. PA30 and PA31 with UpdateCompression.dll (this
// PC's, else the one in `fallbackDlls`: an image's System32 of the same architecture; msdelta.dll
// when there is none: older PA30 only), PA19 with mspatcha.dll — each against an empty source.
// Several at once.
[[nodiscard]] Result<void> expandPsf(const std::filesystem::path& psf, const std::vector<PsfEntry>& entries,
                                     const std::filesystem::path& folder, const TaskContext& task,
                                     const std::filesystem::path& fallbackDlls = {});

// The whole package: `container` (the .wim; wimgapi, so admin) applied into `folder`, then its .psf
// (same name) expanded over it. Returns <folder>\update.mum.
[[nodiscard]] Result<std::filesystem::path> expandUpdatePackage(const std::filesystem::path& container,
                                                                const std::filesystem::path& folder,
                                                                const TaskContext& task,
                                                                const std::filesystem::path& fallbackDlls = {});

} // namespace wl::core::uup
