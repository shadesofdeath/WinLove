#pragma once
// D-110: the reference tool's removal templates as WinLove's catalogs mark them. Each deeper template takes the
// ones before it too (Gizlilik ⊂ Genel ⊂ Hafif); "not recommended" is in no template and the
// "Önerilen" compatibility guard holds it; None = left to the user (drivers, the store cleanup).
// The tree shows the tier as a colored arc left of the check circle (tokens tier.*).
#include <cstdint>
#include <string_view>

namespace wl::app {

enum class Tier : std::uint8_t { None, Privacy, General, Light, NotRecommended };

[[nodiscard]] inline Tier tierFrom(std::string_view text) noexcept {
    if (text == "privacy") {
        return Tier::Privacy;
    }
    if (text == "general") {
        return Tier::General;
    }
    if (text == "light") {
        return Tier::Light;
    }
    if (text == "notRecommended") {
        return Tier::NotRecommended;
    }
    return Tier::None;
}

// The template `chosen` takes `tier` (privacy ⊂ general ⊂ light).
[[nodiscard]] inline bool templateTakes(Tier chosen, Tier tier) noexcept {
    return tier != Tier::None && tier != Tier::NotRecommended && static_cast<int>(tier) <= static_cast<int>(chosen);
}

} // namespace wl::app
