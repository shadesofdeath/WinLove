#pragma once
// The add / edit dialog of one post-setup step (P14): a small form whose rows depend on the
// step type — winget (common apps, name, id), command (name, command line, wait), copy (name,
// source on this PC with "Dosya… / Klasör…", target folder on the installed system).
// The primary button stays disabled until the step can be written (core::validatePostSetup).
#include "app/Localization.h"
#include "core/postsetup/PostSetup.h"
#include "ui/widgets/Dialog.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace wl::app {

struct StepDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // the first text box
};

struct StepDialogActions {
    std::function<std::optional<std::filesystem::path>()> pickFile;   // copy steps
    std::function<std::optional<std::filesystem::path>()> pickFolder;
    std::function<void(core::PostSetupStep)> accept; // called after `close`
    std::function<void()> close;                      // pops the dialog
};

[[nodiscard]] StepDialog makeStepDialog(const Localization& strings, core::PostSetupStep step, bool editing,
                                        StepDialogActions actions);

} // namespace wl::app
