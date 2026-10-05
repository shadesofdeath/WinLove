#pragma once
// P11 "Değer ekle / düzenle" (D-067): one registry value — key, name, type, data — and whether it
// is also re-applied after setup. The primary button stays disabled until the value parses
// (core::registryWriteFromInput) and the image can take its key (RegistryController::importable);
// the message line says what is wrong.
#include "app/Localization.h"
#include "core/image/RegistryEdit.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <optional>

namespace wl::app {

struct ValueDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // the key box
};

struct ValueDialogActions {
    std::function<void(core::RegistryWrite, bool afterSetup)> accept; // called after `close`
    std::function<void()> close;                                      // pops the dialog
};

// `initial`: the value being edited (nullopt: a new one).
[[nodiscard]] ValueDialog makeValueDialog(const Localization& strings, const std::optional<core::RegistryWrite>& initial,
                                          bool afterSetup, ValueDialogActions actions);

} // namespace wl::app
