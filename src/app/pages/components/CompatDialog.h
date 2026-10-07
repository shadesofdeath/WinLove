#pragma once
// D-082: "Uyumluluk" — the compatibility guards as a check list (Koruma · Neyi korur). Saving
// hands back the ids that are on; what then clashes leaves the queue (CompatController).
#include "app/Localization.h"
#include "app/catalog/CompatCatalog.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

struct CompatDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // the list
};

struct CompatDialogActions {
    std::function<void(std::vector<std::wstring>)> save; // called after `close`, with the ids that are on
    std::function<void()> close;                          // pops the dialog
};

[[nodiscard]] CompatDialog makeCompatDialog(const Localization& strings, Language language, const CompatCatalog& catalog,
                                            const std::vector<std::wstring>& on, CompatDialogActions actions);

} // namespace wl::app
