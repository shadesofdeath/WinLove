#pragma once
// 2026-11-11 (the owner: Özet › Kuyruk said "2 değişiklik" with nothing on its left): every queued
// operation goes to the page whose work it is, and that page's tab is a chip of the Kuyruk card —
// so the chips add up to the queue. The navigation's own badges count what each page shows (a
// language, a setting); the card counts operations, as its "N değişiklik" does. Pages outside the
// three groups get theirs too: the edition upgrade (Görüntü), the answer file's welcome (Kurulum ›
// Katılımsız); anything no page claims lands in "Diğer". Pure: the tests check every operation kind.
#include "app/pages/overview/OverviewModel.h"
#include "core/ops/ChangeSet.h"

#include <string>
#include <utility>
#include <vector>

namespace wl::app {

// Slots of the kinds several pages queue (registry values, files) that a page claims as its own.
struct QueueClaims {
    using Slot = std::pair<core::ops::OpKind, std::wstring>;
    std::vector<Slot> registry;   // Kayıt defteri: the values typed or imported there
    std::vector<Slot> unattended; // Katılımsız: the welcome's files (D-084), queued with every mount
    std::vector<Slot> components; // Bileşenler: what a component brings along (the shrink's update policy)
};

// The page that queued `op`: by its kind, and for a shared kind by the claims; registry values and
// files nobody claims are the settings' (Ayarlar), as that page writes most of them.
[[nodiscard]] PageId queuePageOf(const core::ops::Operation& op, const QueueClaims& claims);

// The card's layout: groups of tabs, each tab the pages it holds (the navigation's).
struct QueueTab {
    PageId tab = PageId::Images;
    Str label = Str::NavImages;
    std::vector<PageId> pages;
};
struct QueueLayout {
    Str caption = Str::NavImages;
    std::vector<QueueTab> tabs;
};

// Every operation counted once, in the tab holding its page; one more group ("Diğer") for those in
// no tab. Tabs with nothing queued are kept with count 0 (buildOverview leaves them out).
[[nodiscard]] std::vector<OverviewInputs::QueueGroup> queueGroups(const core::ops::ChangeSet& changes, const QueueClaims& claims,
                                                                  const std::vector<QueueLayout>& layout);

} // namespace wl::app
