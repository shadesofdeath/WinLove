#include "core/policy/Admx.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <format>

namespace wl::core::policy {

namespace {

std::string_view localName(const char* name) {
    const std::string_view n(name);
    const auto colon = n.find(':');
    return colon == std::string_view::npos ? n : n.substr(colon + 1);
}

// Children with this local name (the files may carry a default xmlns, never a prefix in practice).
template <class F>
void eachChild(const pugi::xml_node& node, std::string_view name, F&& f) {
    for (const auto& child : node.children()) {
        if (child.type() == pugi::node_element && localName(child.name()) == name) {
            f(child);
        }
    }
}

pugi::xml_node child(const pugi::xml_node& node, std::string_view name) {
    for (const auto& c : node.children()) {
        if (c.type() == pugi::node_element && localName(c.name()) == name) {
            return c;
        }
    }
    return {};
}

std::wstring attr(const pugi::xml_node& node, const char* name) {
    return utf8::toWide(node.attribute(name).as_string());
}

bool flag(const pugi::xml_node& node, const char* name) {
    const std::string_view v = node.attribute(name).as_string();
    return v == "true" || v == "1";
}

std::uint64_t number(const pugi::xml_node& node, const char* name, std::uint64_t fallback) {
    const std::string_view v = node.attribute(name).as_string();
    std::uint64_t out = fallback;
    if (!v.empty()) {
        std::from_chars(v.data(), v.data() + v.size(), out);
    }
    return out;
}

// Whitespace around a text node's content (ADML strings are indented in some files).
std::wstring trimmed(std::wstring s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(L" \t\r\n");
    return s.substr(first, last - first + 1);
}

struct Texts {
    std::map<std::wstring, std::wstring> strings;
    std::map<std::wstring, pugi::xml_node> presentations;
    pugi::xml_document doc; // keeps the presentation nodes alive

    // "$(string.X)" → its text; anything else as it is.
    [[nodiscard]] std::wstring resolve(const std::wstring& ref) const {
        constexpr std::wstring_view kPrefix = L"$(string.";
        if (ref.starts_with(kPrefix) && ref.ends_with(L")")) {
            const std::wstring id = ref.substr(kPrefix.size(), ref.size() - kPrefix.size() - 1);
            const auto it = strings.find(id);
            return it != strings.end() ? it->second : id;
        }
        return ref;
    }
    [[nodiscard]] pugi::xml_node presentation(const std::wstring& ref) const {
        constexpr std::wstring_view kPrefix = L"$(presentation.";
        if (ref.starts_with(kPrefix) && ref.ends_with(L")")) {
            const auto it = presentations.find(ref.substr(kPrefix.size(), ref.size() - kPrefix.size() - 1));
            return it != presentations.end() ? it->second : pugi::xml_node();
        }
        return {};
    }
};

void loadTexts(Texts& texts, std::string_view adml) {
    if (adml.empty() || !texts.doc.load_buffer(adml.data(), adml.size())) {
        return;
    }
    const auto resources = child(texts.doc.document_element(), "resources");
    eachChild(child(resources, "stringTable"), "string", [&](const pugi::xml_node& s) {
        texts.strings[attr(s, "id")] = trimmed(utf8::toWide(s.child_value()));
    });
    eachChild(child(resources, "presentationTable"), "presentation",
              [&](const pugi::xml_node& p) { texts.presentations[attr(p, "id")] = p; });
}

// prefix → namespace of one file.
struct Namespaces {
    std::wstring target;
    std::map<std::wstring, std::wstring> prefixes;
    // "windows:WindowsComponents" → "Microsoft.Policies.Windows:WindowsComponents"; no prefix: the file's.
    [[nodiscard]] std::wstring qualify(const std::wstring& ref) const {
        const auto colon = ref.find(L':');
        if (colon == std::wstring::npos) {
            return target + L":" + ref;
        }
        const auto it = prefixes.find(ref.substr(0, colon));
        return (it != prefixes.end() ? it->second : ref.substr(0, colon)) + L":" + ref.substr(colon + 1);
    }
};

PolicyValue valueOf(const pugi::xml_node& holder) {
    PolicyValue v;
    if (!holder) {
        return v;
    }
    for (const auto& c : holder.children()) {
        if (c.type() != pugi::node_element) {
            continue;
        }
        const auto name = localName(c.name());
        if (name == "decimal") {
            v.kind = PolicyValue::Kind::Decimal;
            v.number = number(c, "value", 0);
        } else if (name == "longDecimal") {
            v.kind = PolicyValue::Kind::LongDecimal;
            v.number = number(c, "value", 0);
        } else if (name == "string") {
            v.kind = PolicyValue::Kind::String;
            v.text = utf8::toWide(c.child_value());
        } else if (name == "delete") {
            v.kind = PolicyValue::Kind::Delete;
        } else {
            continue;
        }
        break;
    }
    return v;
}

PolicyValueList listOf(const pugi::xml_node& node) {
    PolicyValueList list;
    if (!node) {
        return list;
    }
    list.key = attr(node, "defaultKey");
    eachChild(node, "item", [&](const pugi::xml_node& item) {
        list.items.push_back({attr(item, "key"), attr(item, "valueName"), valueOf(child(item, "value"))});
    });
    return list;
}

PolicyElement elementOf(const pugi::xml_node& node, const Texts& texts) {
    PolicyElement e;
    const auto name = localName(node.name());
    e.id = attr(node, "id");
    e.key = attr(node, "key");
    e.valueName = attr(node, "valueName");
    e.required = flag(node, "required");
    if (name == "boolean") {
        e.kind = PolicyElement::Kind::Boolean;
        e.trueValue = valueOf(child(node, "trueValue"));
        e.falseValue = valueOf(child(node, "falseValue"));
        e.trueList = listOf(child(node, "trueList"));
        e.falseList = listOf(child(node, "falseList"));
    } else if (name == "decimal" || name == "longDecimal") {
        e.kind = name == "decimal" ? PolicyElement::Kind::Decimal : PolicyElement::Kind::LongDecimal;
        e.minValue = number(node, "minValue", 0);
        e.maxValue = number(node, "maxValue", name == "decimal" ? 9999 : 0xFFFFFFFFFFFFFFFFull);
        e.storeAsText = flag(node, "storeAsText");
    } else if (name == "text") {
        e.kind = PolicyElement::Kind::Text;
        e.expandable = flag(node, "expandable");
        e.maxLength = static_cast<std::uint32_t>(number(node, "maxLength", 1023));
    } else if (name == "multiText") {
        e.kind = PolicyElement::Kind::MultiText;
        e.maxLength = static_cast<std::uint32_t>(number(node, "maxLength", 1023));
    } else if (name == "enum") {
        e.kind = PolicyElement::Kind::Enum;
        eachChild(node, "item", [&](const pugi::xml_node& item) {
            e.items.push_back({texts.resolve(attr(item, "displayName")), valueOf(child(item, "value")),
                               listOf(child(item, "valueList"))});
        });
    } else if (name == "list") {
        e.kind = PolicyElement::Kind::List;
        e.valuePrefix = attr(node, "valuePrefix");
        e.explicitValue = flag(node, "explicitValue");
        e.additive = flag(node, "additive");
        e.expandable = flag(node, "expandable");
    }
    return e;
}

std::vector<PresentationControl> presentationOf(const pugi::xml_node& node) {
    std::vector<PresentationControl> controls;
    if (!node) {
        return controls;
    }
    using Kind = PresentationControl::Kind;
    for (const auto& c : node.children()) {
        if (c.type() != pugi::node_element) {
            continue;
        }
        const auto name = localName(c.name());
        PresentationControl control;
        control.refId = attr(c, "refId");
        if (name == "text") {
            control.kind = Kind::Text;
            control.label = trimmed(utf8::toWide(c.child_value()));
        } else if (name == "checkBox") {
            control.kind = Kind::CheckBox;
            control.label = trimmed(utf8::toWide(c.child_value()));
            control.defaultChecked = flag(c, "defaultChecked");
        } else if (name == "dropdownList") {
            control.kind = Kind::DropdownList;
            control.label = trimmed(utf8::toWide(c.child_value()));
            control.defaultItem = c.attribute("defaultItem") ? static_cast<int>(number(c, "defaultItem", 0)) : -1;
        } else if (name == "decimalTextBox" || name == "longDecimalTextBox") {
            control.kind = name == "decimalTextBox" ? Kind::DecimalTextBox : Kind::LongDecimalTextBox;
            control.label = trimmed(utf8::toWide(c.child_value()));
            control.hasDefaultValue = static_cast<bool>(c.attribute("defaultValue"));
            control.defaultValue = number(c, "defaultValue", 0);
        } else if (name == "textBox" || name == "comboBox") {
            control.kind = name == "textBox" ? Kind::TextBox : Kind::ComboBox;
            control.label = trimmed(utf8::toWide(child(c, "label").child_value()));
            control.defaultText =
                trimmed(utf8::toWide(child(c, name == "textBox" ? "defaultValue" : "default").child_value()));
            eachChild(c, "suggestion", [&](const pugi::xml_node& s) {
                control.suggestions.push_back(trimmed(utf8::toWide(s.child_value())));
            });
        } else if (name == "multiTextBox") {
            control.kind = Kind::MultiTextBox;
            control.label = trimmed(utf8::toWide(c.child_value()));
        } else if (name == "listBox") {
            control.kind = Kind::ListBox;
            control.label = trimmed(utf8::toWide(c.child_value()));
        } else {
            continue;
        }
        controls.push_back(std::move(control));
    }
    return controls;
}

std::wstring lowered(std::wstring_view s) {
    return wl::text::lower(std::wstring(s));
}

} // namespace

const PolicyElement* Policy::element(std::wstring_view elementId) const {
    const auto it = std::ranges::find_if(elements, [&](const PolicyElement& e) { return e.id == elementId; });
    return it != elements.end() ? &*it : nullptr;
}

const Category* PolicyCatalog::category(std::wstring_view id) const {
    const auto it = m_categoryIndex.find(lowered(id));
    return it != m_categoryIndex.end() ? &m_categories[it->second] : nullptr;
}

const Policy* PolicyCatalog::policy(std::wstring_view id) const {
    const auto it = m_policyIndex.find(lowered(id));
    return it != m_policyIndex.end() ? &m_policies[it->second] : nullptr;
}

std::vector<std::wstring> PolicyCatalog::path(const Policy& policy) const {
    std::vector<std::wstring> names;
    const Category* c = category(policy.category);
    for (int guard = 0; c && guard < 16; ++guard) { // a loop in broken files ends here
        names.push_back(c->displayName);
        c = c->parent.empty() ? nullptr : category(c->parent);
    }
    std::ranges::reverse(names);
    return names;
}

std::wstring PolicyCatalog::rootOf(const Policy& policy) const {
    const Category* c = category(policy.category);
    for (int guard = 0; c && !c->parent.empty() && guard < 16; ++guard) {
        const Category* up = category(c->parent);
        if (!up) {
            break;
        }
        c = up;
    }
    return c ? c->id : std::wstring();
}

Result<void> PolicyCatalog::add(std::string_view admx, std::string_view adml, std::wstring_view file) {
    pugi::xml_document doc;
    if (const auto parsed = doc.load_buffer(admx.data(), admx.size()); !parsed) {
        return fail(ErrorCode::InvalidArgument, L"ADMX does not parse: " + utf8::toWide(parsed.description()),
                    std::wstring(file));
    }
    const auto root = doc.document_element();
    if (localName(root.name()) != "policyDefinitions") {
        return fail(ErrorCode::InvalidArgument, L"not an ADMX file (no policyDefinitions)", std::wstring(file));
    }
    Texts texts;
    loadTexts(texts, adml);

    Namespaces ns;
    {
        const auto names = child(root, "policyNamespaces");
        const auto target = child(names, "target");
        ns.target = attr(target, "namespace");
        ns.prefixes[attr(target, "prefix")] = ns.target;
        eachChild(names, "using", [&](const pugi::xml_node& u) { ns.prefixes[attr(u, "prefix")] = attr(u, "namespace"); });
    }
    if (ns.target.empty()) {
        return fail(ErrorCode::InvalidArgument, L"ADMX without a target namespace", std::wstring(file));
    }
    eachChild(child(child(root, "supportedOn"), "definitions"), "definition", [&](const pugi::xml_node& d) {
        m_supported[lowered(ns.qualify(attr(d, "name")))] = texts.resolve(attr(d, "displayName"));
    });
    eachChild(child(root, "categories"), "category", [&](const pugi::xml_node& c) {
        Category category;
        category.id = ns.qualify(attr(c, "name"));
        category.displayName = texts.resolve(attr(c, "displayName"));
        if (const auto parent = child(c, "parentCategory")) {
            category.parent = ns.qualify(attr(parent, "ref"));
        }
        if (m_categoryIndex.emplace(lowered(category.id), m_categories.size()).second) {
            m_categories.push_back(std::move(category));
        }
    });
    eachChild(child(root, "policies"), "policy", [&](const pugi::xml_node& p) {
        Policy policy;
        policy.name = attr(p, "name");
        policy.id = ns.qualify(policy.name);
        const std::string_view cls = p.attribute("class").as_string();
        policy.policyClass = cls == "User" ? PolicyClass::User : cls == "Both" ? PolicyClass::Both : PolicyClass::Machine;
        policy.displayName = texts.resolve(attr(p, "displayName"));
        policy.explain = texts.resolve(attr(p, "explainText"));
        policy.key = attr(p, "key");
        policy.valueName = attr(p, "valueName");
        policy.file = std::wstring(file);
        if (const auto parent = child(p, "parentCategory")) {
            policy.category = ns.qualify(attr(parent, "ref"));
        }
        if (const auto supported = child(p, "supportedOn")) {
            policy.supportedOn = ns.qualify(attr(supported, "ref")); // resolved to its text in finish()
        }
        policy.enabledValue = valueOf(child(p, "enabledValue"));
        policy.disabledValue = valueOf(child(p, "disabledValue"));
        policy.enabledList = listOf(child(p, "enabledList"));
        policy.disabledList = listOf(child(p, "disabledList"));
        for (const auto& e : child(p, "elements").children()) {
            if (e.type() == pugi::node_element) {
                policy.elements.push_back(elementOf(e, texts));
            }
        }
        policy.presentation = presentationOf(texts.presentation(attr(p, "presentation")));
        if (policy.key.empty() || policy.name.empty()) {
            return; // nothing it could write
        }
        if (m_policyIndex.emplace(lowered(policy.id), m_policies.size()).second) {
            m_policies.push_back(std::move(policy));
        }
    });
    return {};
}

void PolicyCatalog::finish() {
    for (auto& policy : m_policies) {
        if (!policy.supportedOn.empty()) {
            const auto it = m_supported.find(lowered(policy.supportedOn));
            policy.supportedOn = it != m_supported.end() ? it->second : std::wstring();
        }
    }
    m_supported.clear();
}

Result<PolicyCatalog> readPolicyDefinitions(const std::filesystem::path& folder, std::wstring_view language) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return fail(ErrorCode::NotFound, L"no PolicyDefinitions folder in the image", folder.wstring());
    }
    std::vector<std::filesystem::path> admx;
    std::vector<std::filesystem::path> languages; // the "ll-CC" folders, preferred first
    for (auto it = std::filesystem::directory_iterator(folder, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        if (it->is_directory(ec)) {
            languages.push_back(it->path());
        } else if (wl::text::iequals(it->path().extension().wstring(), L".admx")) {
            admx.push_back(it->path());
        }
    }
    auto rank = [&](const std::filesystem::path& dir) {
        const std::wstring name = dir.filename().wstring();
        return !language.empty() && wl::text::iequals(name, language) ? 0 : wl::text::iequals(name, L"en-US") ? 1 : 2;
    };
    std::ranges::stable_sort(languages, [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    std::ranges::sort(admx);

    PolicyCatalog catalog;
    int skipped = 0;
    for (const auto& file : admx) {
        auto bytes = readFileBytes(file);
        if (!bytes) {
            ++skipped;
            continue;
        }
        std::string adml;
        const auto stem = file.stem().wstring() + L".adml";
        for (const auto& dir : languages) {
            if (std::filesystem::exists(dir / stem, ec)) {
                if (auto texts = readFileBytes(dir / stem)) {
                    adml = std::move(*texts);
                    break;
                }
            }
        }
        if (auto r = catalog.add(*bytes, adml, file.filename().wstring()); !r) {
            log::warn("policy", describe(r.error()));
            ++skipped;
        }
    }
    catalog.finish();
    log::info("policy", std::format(L"{} policies, {} categories from {} ADMX files ({} skipped) in {}",
                                    catalog.policies().size(), catalog.categories().size(), admx.size(), skipped,
                                    folder.wstring()));
    return catalog;
}

} // namespace wl::core::policy
