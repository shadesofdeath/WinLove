#pragma once
// D-058: the small dialogs of the Images page tools and the Kaynak page — one choice, one text,
// a list of editions, a capture's name, a file's SHA-256. Each returns the dialog and the widget to
// focus; `close` pops it, the accept callback runs after `close` (the dialog is gone by then).
#include "app/Localization.h"
#include "core/image/ImageInfo.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wl::app {

struct ToolDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr;
};

// One of `options` (a dropdown) and a hint line under it.
[[nodiscard]] ToolDialog makeChoiceDialog(const Localization& strings, std::wstring title, std::wstring note, std::wstring label,
                                          std::vector<std::wstring> options, int selected, std::wstring acceptLabel,
                                          std::function<void(int)> accept, std::function<void()> close);

// One line of text; the primary button waits until it is not blank.
[[nodiscard]] ToolDialog makeTextDialog(const Localization& strings, std::wstring title, std::wstring note, std::wstring label,
                                        std::wstring text, std::wstring acceptLabel, std::function<void(std::wstring)> accept,
                                        std::function<void()> close);

// The editions of another image, each a check box (all ticked); accepted with the ticked indexes.
[[nodiscard]] ToolDialog makeEditionsDialog(const Localization& strings, std::wstring title, std::wstring note,
                                            const std::vector<core::ImageInfo>& editions, std::wstring acceptLabel,
                                            std::function<void(std::vector<int>)> accept, std::function<void()> close);

// Name, description and compression (0 LZX, 1 XPRESS) of a capture.
struct CaptureAnswers {
    std::wstring name;
    std::wstring description;
    int compression = 0;
};
[[nodiscard]] ToolDialog makeCaptureDialog(const Localization& strings, std::wstring note, std::wstring name,
                                           std::function<void(CaptureAnswers)> accept, std::function<void()> close);

// A file's SHA-256 as it is computed, and a box to paste the published one into: says whether
// they match. `update` (UI thread) gives progress (0..1) until the hash arrives (or an error).
struct HashDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr;
    std::function<void(double fraction)> progress;
    std::function<void(std::wstring hash)> done;
    std::function<void(std::wstring error)> failed;
};
[[nodiscard]] HashDialog makeHashDialog(const Localization& strings, std::wstring file, std::function<void(std::wstring)> copy,
                                        std::function<void()> close);

} // namespace wl::app
