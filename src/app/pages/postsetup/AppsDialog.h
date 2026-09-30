#pragma once
// "Hazır uygulamalar" (P14): the catalog of well-known winget packages as a check list — tick
// several, one button adds them all as winget steps. Apps that already are a step are shown
// ticked and cannot be added twice. Space / click toggles a row.
#include "app/Localization.h"
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

struct AppsDialogActions {
    std::function<bool(std::size_t index)> present;            // PostSetupController::popularApps()[index] is a step already
    std::function<void(std::vector<std::size_t>)> accept;      // called after `close`, with the ticked indexes
    std::function<void()> close;                               // pops the dialog
};

[[nodiscard]] AppsDialog makeAppsDialog(const Localization& strings, std::wstring body, AppsDialogActions actions);

} // namespace wl::app
