#pragma once
// P07 (D-059): what the CBS packages of a mounted image carry, so the Components page can say
// whether a package-level component is there and how much removing it frees.
//
//   installed   SOFTWARE\…\Component Based Servicing\Packages\<identity>: CurrentState >= 0x70.
//   tree        Windows\servicing\Packages\<identity>.mum: a package's child packages
//               (<package><assemblyIdentity name=…/>, at any depth of the manifest).
//   ownership   COMPONENTS hive: DerivedData\Components\<component> has a "c!<deployment>" value
//               per deployment that owns it; CanonicalData\Deployments\<deployment> has an
//               "i!CBS_<package>" value per package that installs it (data: int32 length, int32
//               flags, the package identity in ASCII). Cumulative updates (Package_for_RollupFix,
//               Package_N_for_KB…) re-own every component they updated and are not owners here.
//   bytes       Windows\WinSxS\<component folder>, summed by component name (version, culture and
//               hash dropped) — the payload a removal takes out of the image.
// A component is exclusive to a set of package trees when every owner is inside them: those
// bytes are what removing the set frees (the WIM keeps one copy of hard-linked files anyway).
// Everything is read through offreg.dll and directory listings: no hive is loaded, DISM may keep
// its session open.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wl::core {

class ComponentStoreIndex {
public:
    // Elevated process (a mounted image's servicing folders are closed to other users).
    [[nodiscard]] static Result<ComponentStoreIndex> build(const std::filesystem::path& mountDir, const TaskContext& task);

    // At least one identity of the family is installed (case does not matter).
    [[nodiscard]] bool installed(std::wstring_view family) const;
    // Bytes only the union of these families' package trees owns.
    [[nodiscard]] std::uint64_t exclusiveBytes(const std::vector<std::wstring>& families) const;

    // ---- building blocks, public for tests ---------------------------------------------------
    // "amd64_microsoft-windows-foo_31bf3856ad364e35_10.0.26100.1_none_0123abcd" → "amd64_microsoft-windows-foo"
    [[nodiscard]] static std::wstring componentName(std::wstring_view folderOrKey);
    // The identity in an "i!CBS_…" deployment value; empty when the data is not one.
    [[nodiscard]] static std::wstring deploymentPackage(const std::vector<std::uint8_t>& data);
    // Package_for_RollupFix, Package_12_for_KB5066128, Package_for_ServicingStack_8035 …
    [[nodiscard]] static bool isUpdatePackage(std::wstring_view family);
    // Child package names in a .mum (lower case).
    [[nodiscard]] static std::vector<std::wstring> manifestChildren(std::string_view xml);

    // Direct construction (tests): families lower case.
    void addInstalled(std::wstring family);
    void addChild(std::wstring parent, std::wstring child);
    void addOwner(std::wstring component, std::wstring family);
    void addBytes(std::wstring component, std::uint64_t bytes);

private:
    [[nodiscard]] std::set<std::wstring> tree(const std::vector<std::wstring>& families) const;

    std::set<std::wstring> m_installed;
    std::unordered_map<std::wstring, std::set<std::wstring>> m_children;
    std::unordered_map<std::wstring, std::set<std::wstring>> m_owners; // component -> families
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_owned; // family -> components
    std::unordered_map<std::wstring, std::uint64_t> m_bytes;
};

} // namespace wl::core
