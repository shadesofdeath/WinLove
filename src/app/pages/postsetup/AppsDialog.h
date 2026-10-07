#pragma once
// The check-list dialogs of P14: "Hazır uygulamalar" (well-known winget packages) and "Hazır
// komutlar" (power plan, network …) — tick several, one button adds them all as steps. Rows that
// already are a step are shown ticked and cannot be added twice. Space / click toggles a row.
#include "app/Localization.h"
#include "app/controllers/PostSetupController.h"
#include "ui/widgets/Dialog.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wl::app {

struct AppsDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // the list
};

struct CatalogRow {
    std::wstring name;
    std::wstring category;
    std::wstring detail; // mono: winget id, command line
};

struct CatalogDialogSpec {
    std::wstring title;
    std::wstring body;
    std::wstring detailColumn;
    Str addFormat = Str::PostsetupCatalogAdd; // "{n} … ekle"
    std::vector<CatalogRow> rows;
    std::wstring nameColumn; // empty: "Uygulama"
};

struct AppsDialogActions {
    std::function<bool(std::size_t index)> present;            // row `index` is a step already
    std::function<void(std::vector<std::size_t>)> accept;      // called after `close`, with the ticked indexes
    std::function<void()> close;                               // pops the dialog
};

[[nodiscard]] AppsDialog makeCatalogDialog(const Localization& strings, CatalogDialogSpec spec, AppsDialogActions actions);

// Row texts of PostSetupController's ready commands.
[[nodiscard]] std::vector<CatalogRow> commandRows(const Localization& strings, Language language);

} // namespace wl::app
