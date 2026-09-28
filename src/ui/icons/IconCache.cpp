#include "ui/icons/IconCache.h"

#include "base/Hresult.h"
#include "ui/render/SvgPath.h"

namespace wl::ui {

Result<std::unique_ptr<IconCache>> IconCache::create(ID2D1Factory3* factory) {
    auto cache = std::make_unique<IconCache>();
    cache->m_factory = factory;
    // icons.md: round caps and joins for every icon.
    const auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                   D2D1_LINE_JOIN_ROUND);
    WL_TRY_HR(factory->CreateStrokeStyle(props, nullptr, 0, &cache->m_stroke), ErrorCode::RenderFailure,
              L"creating icon stroke style");
    return cache;
}

IconCache::Entry IconCache::get(icons::Icon icon, IconVariant variant) {
    const auto index = static_cast<std::size_t>(icon);
    if (index >= icons::kIconCount) {
        return {};
    }
    const auto& def = icons::kIcons[index];
    const icons::IconVariant& data = variant == IconVariant::Filled16    ? def.filled16
                                     : variant == IconVariant::Regular24 ? def.regular24
                                                                         : def.regular16;
    const std::size_t slot = index * 3 + static_cast<std::size_t>(variant);
    if (!m_geometries[slot] && !m_failed[slot]) {
        if (auto geometry = buildSvgPath(m_factory.Get(), data.pathData, /*fillEvenOdd=*/true)) {
            m_geometries[slot] = std::move(*geometry);
        } else {
            m_failed[slot] = true;
        }
    }
    return {m_geometries[slot].Get(), data.strokeWidth, data.filled,
            variant == IconVariant::Regular24 ? 24.0f : 16.0f};
}

} // namespace wl::ui
