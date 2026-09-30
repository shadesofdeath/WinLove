#pragma once
// "Yeniden adlandır…" of the Images page (P02): the name and the description of an edition — what
// DISM and the edition list of Windows Setup show. "Kaydet" stays disabled while the name is empty
// or nothing has changed.
#include "app/Localization.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <string>

namespace wl::app {

struct RenameDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // the name box
};

struct RenameDialogActions {
    std::function<void(std::wstring name, std::wstring description)> accept; // called after `close`
    std::function<void()> close;                                              // pops the dialog
};

// `note`: a line under the title (e.g. that an ISO is copied to the work folder first).
[[nodiscard]] RenameDialog makeRenameDialog(const Localization& strings, std::wstring name, std::wstring description,
                                            std::wstring note, RenameDialogActions actions);

} // namespace wl::app
