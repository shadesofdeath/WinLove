#pragma once
// D-046: what the Microsoft Update Catalog offers for the mounted image — a check list
// (Güncelleme · KB · Tarih · Boyut · Not). The newest released cumulative and .NET updates come
// ticked; a newer preview is offered unticked; an update the image already has (same build,
// revision not newer) cannot be ticked. The primary button names the count and the total size.
#include "app/Localization.h"
#include "core/updates/UpdateCatalog.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

struct UpdateCatalogDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // the list
};

struct UpdateCatalogActions {
    std::function<void(std::vector<core::CatalogEntry>)> download; // called after `close`, with the ticked entries
    std::function<void()> close;                                   // pops the dialog
};

[[nodiscard]] UpdateCatalogDialog makeUpdateCatalogDialog(const Localization& strings, Language language,
                                                    const core::CatalogTarget& target,
                                                    std::vector<core::CatalogOffer> offers, UpdateCatalogActions actions);

} // namespace wl::app
