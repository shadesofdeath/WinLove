#pragma once
// D-110: the reference tool's "Kaldırma modu" — the removal level (Varsayılan+ · Derin · Maksimum) as a radio
// list with what each does to the component store. "Tamam" hands back the level.
#include "app/Localization.h"
#include "app/controllers/ComponentController.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>

namespace wl::app {

struct ModeDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr;
};

struct ModeDialogActions {
    std::function<void(ComponentController::Level)> save; // called after `close`
    std::function<void()> close;
};

[[nodiscard]] ModeDialog makeModeDialog(const Localization& strings, ComponentController::Level level, ModeDialogActions actions);

} // namespace wl::app
