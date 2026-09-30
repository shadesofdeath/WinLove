#pragma once
// "Sürümü yükselt…" of the Images page (P02, D-035): the editions the mounted image can become
// (core::readEditions), one of them picked for the queue. Nothing happens to the image here: the
// change is an operation like any other and runs as the first step of "Uygula".
#include "app/Localization.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wl::app {

struct EditionChoice {
    std::wstring id;   // "Professional"
    std::wstring name; // "Windows 11 Pro"
};

struct EditionDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // "Kuyruğa ekle": Enter takes the preselected edition
};

struct EditionDialogActions {
    std::function<void(std::wstring editionId)> accept; // called after `close`
    std::function<void()> remove;                       // take the queued change back (after `close`)
    std::function<void()> close;                        // pops the dialog
};

// `body`: what the image is now and what the change means. `queued`: the id already in the queue
// (empty: none) — it is preselected and "Kuyruktan çıkar" is offered.
[[nodiscard]] EditionDialog makeEditionDialog(const Localization& strings, std::wstring body,
                                             std::vector<EditionChoice> choices, const std::wstring& queued,
                                             EditionDialogActions actions);

} // namespace wl::app
