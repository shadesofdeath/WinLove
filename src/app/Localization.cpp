#include "app/Localization.h"

#include "base/Utf8.h"

#include <json.hpp>

#include <unordered_map>

namespace wl::app {

namespace {

void flatten(const nlohmann::json& node, const std::string& prefix,
             std::unordered_map<std::string, std::string>& out) {
    for (const auto& [key, value] : node.items()) {
        const std::string path = prefix.empty() ? key : prefix + "." + key;
        if (value.is_object()) {
            flatten(value, path, out);
        } else if (value.is_string()) {
            out.emplace(path, value.get<std::string>());
        }
    }
}

} // namespace

Result<Localization> Localization::fromJson(std::string_view utf8Json) {
    const auto root = nlohmann::json::parse(utf8Json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        return fail(ErrorCode::ParseError, L"strings file is not a JSON object");
    }
    std::unordered_map<std::string, std::string> flat;
    flatten(root, {}, flat);

    Localization loc;
    for (std::size_t i = 0; i < kStrCount; ++i) {
        const auto it = flat.find(kStrKeys[i]);
        if (it == flat.end()) {
            return fail(ErrorCode::NotFound, L"missing string key", utf8::toWide(kStrKeys[i]));
        }
        loc.m_values[i] = utf8::toWide(it->second);
        flat.erase(it);
    }
    if (!flat.empty()) {
        return fail(ErrorCode::ParseError, L"unknown string key (run ./build.ps1 -Gen)",
                    utf8::toWide(flat.begin()->first));
    }
    return loc;
}

const std::wstring& Localization::get(Str key) const noexcept {
    return m_values[static_cast<std::size_t>(key)];
}

std::wstring Localization::format(Str key, std::initializer_list<Arg> args) const {
    std::wstring text = get(key);
    for (const auto& [name, value] : args) {
        const std::wstring token = L"{" + std::wstring(name) + L"}";
        for (auto pos = text.find(token); pos != std::wstring::npos; pos = text.find(token, pos + value.size())) {
            text.replace(pos, token.size(), value);
        }
    }
    return text;
}

} // namespace wl::app
