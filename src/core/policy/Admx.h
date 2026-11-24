#pragma once
// D-154 Grup İlkesi: the image's own Administrative Templates — Windows\PolicyDefinitions\*.admx with
// the texts of <language>\*.adml — read into a catalog, so the page lists what this Windows build
// knows, in its language, with no list of ours to keep up to date.
//
// The model follows the ADMX schema (Microsoft's "Group Policy ADMX syntax reference"):
//   policy  — class Machine / User / Both, key, optional valueName with enabledValue / disabledValue,
//             enabledList / disabledList, elements (boolean, decimal, longDecimal, text, multiText,
//             enum, list), a presentation in the ADML (the controls gpedit shows for the elements);
//   category — a tree across files: "prefix:name" refs resolve through each file's
//             <policyNamespaces> (target + using) to "<namespace>:<name>".
// Ids are "<namespace>:<name>" ("Microsoft.Policies.WindowsUpdate:AutoUpdateCfg"): unique across files.
// A file that does not parse is skipped (logged); a missing ADML leaves its names as the ids.
#include "base/Result.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core::policy {

enum class PolicyClass : std::uint8_t { Machine, User, Both };

struct PolicyValue {
    enum class Kind : std::uint8_t { None, Decimal, LongDecimal, String, Delete };
    Kind kind = Kind::None;
    std::uint64_t number = 0;
    std::wstring text;
    [[nodiscard]] bool operator==(const PolicyValue&) const = default;
};

struct PolicyValueItem {
    std::wstring key; // empty: the list's default key
    std::wstring valueName;
    PolicyValue value;
};

struct PolicyValueList {
    std::wstring key; // "defaultKey"; empty: the policy's (or element's) key
    std::vector<PolicyValueItem> items;
    [[nodiscard]] bool empty() const noexcept { return items.empty(); }
};

struct PolicyElement {
    enum class Kind : std::uint8_t { Boolean, Decimal, LongDecimal, Text, MultiText, Enum, List };
    struct EnumItem {
        std::wstring displayName;
        PolicyValue value;
        PolicyValueList valueList;
    };
    Kind kind = Kind::Text;
    std::wstring id;
    std::wstring key; // empty: the policy's
    std::wstring valueName;
    bool required = false;
    // decimal / longDecimal
    std::uint64_t minValue = 0;
    std::uint64_t maxValue = 9999;
    bool storeAsText = false;
    // text / list / multiText
    bool expandable = false;
    std::uint32_t maxLength = 1023;
    // boolean
    PolicyValue trueValue;
    PolicyValue falseValue;
    PolicyValueList trueList;
    PolicyValueList falseList;
    // enum
    std::vector<EnumItem> items;
    // list
    std::wstring valuePrefix;
    bool explicitValue = false;
    bool additive = false;
};

// One control of the policy's presentation (ADML), in order.
struct PresentationControl {
    enum class Kind : std::uint8_t { Text, CheckBox, DropdownList, DecimalTextBox, LongDecimalTextBox, TextBox,
                                     MultiTextBox, ListBox, ComboBox };
    Kind kind = Kind::Text;
    std::wstring refId; // the element it edits (empty for Text)
    std::wstring label;
    bool defaultChecked = false;
    int defaultItem = -1;          // dropdownList
    std::uint64_t defaultValue = 0; // decimal boxes
    bool hasDefaultValue = false;
    std::wstring defaultText;      // textBox / comboBox
    std::vector<std::wstring> suggestions; // comboBox
};

struct Category {
    std::wstring id;          // "<namespace>:<name>"
    std::wstring displayName;
    std::wstring parent;      // its parent's id; empty: a root ("Windows Bileşenleri", "Sistem" …)
};

struct Policy {
    std::wstring id;          // "<namespace>:<name>"
    std::wstring name;        // the ADMX name ("AutoUpdateCfg")
    PolicyClass policyClass = PolicyClass::Machine;
    std::wstring displayName;
    std::wstring explain;
    std::wstring supportedOn; // display text ("En az Windows 10")
    std::wstring category;    // id
    std::wstring key;         // "Software\Policies\Microsoft\Windows\WindowsUpdate\AU" (no root)
    std::wstring valueName;   // empty: the elements / lists hold the values
    PolicyValue enabledValue;
    PolicyValue disabledValue;
    PolicyValueList enabledList;
    PolicyValueList disabledList;
    std::vector<PolicyElement> elements;
    std::vector<PresentationControl> presentation;
    std::wstring file;        // "WindowsUpdate.admx"

    [[nodiscard]] const PolicyElement* element(std::wstring_view elementId) const;
};

class PolicyCatalog {
public:
    [[nodiscard]] const std::vector<Policy>& policies() const noexcept { return m_policies; }
    [[nodiscard]] const std::vector<Category>& categories() const noexcept { return m_categories; }
    [[nodiscard]] const Category* category(std::wstring_view id) const;
    [[nodiscard]] const Policy* policy(std::wstring_view id) const;
    // The category names from the root down to the policy's own ("Windows Bileşenleri", "Windows Update").
    [[nodiscard]] std::vector<std::wstring> path(const Policy& policy) const;
    // The root category's id of a policy (empty when it has none).
    [[nodiscard]] std::wstring rootOf(const Policy& policy) const;

    // One ADMX file with its ADML (`adml` may be empty). Unit tests feed texts; read() files.
    Result<void> add(std::string_view admx, std::string_view adml, std::wstring_view file);
    // Ties what files referred to across each other (supportedOn texts) and sorts; after the last add().
    void finish();

private:
    std::vector<Policy> m_policies;
    std::vector<Category> m_categories;
    std::map<std::wstring, std::size_t> m_policyIndex;   // id → position
    std::map<std::wstring, std::size_t> m_categoryIndex; // id → position
    std::map<std::wstring, std::wstring> m_supported;    // definition id → display text
};

// Reads <folder>\*.admx; the texts from <folder>\<language>\<name>.adml, else en-US, else the first
// language folder that has the file. `language` empty: en-US first.
[[nodiscard]] Result<PolicyCatalog> readPolicyDefinitions(const std::filesystem::path& folder, std::wstring_view language);

} // namespace wl::core::policy
