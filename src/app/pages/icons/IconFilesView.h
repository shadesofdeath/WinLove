#pragma once
// D-068 Simgeler › Sistem simgeleri: left, the image's icon files (search, "n değişiklik" /
// "imajda yamalı" badges); right, every icon of the selected file in a grid — the image's icon, or
// the picked replacement framed in accent. Click selects, Enter / double click picks a new icon,
// Delete goes back to the original; the buttons above the grid do the same plus "save as .ico"
// and "restore the image's original" for a file an earlier Uygula patched.
#include "app/Localization.h"
#include "app/controllers/IconPatchController.h"
#include "ui/widget/Widget.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace wl::ui {
class Button;
class SearchBox;
} // namespace wl::ui

namespace wl::app {

class IconFilesView : public ui::Widget {
public:
    struct Intents {
        std::function<std::optional<std::filesystem::path>()> pickSource;            // .ico or a picture
        std::function<std::optional<std::filesystem::path>(std::wstring)> saveIco;    // suggested name
        std::function<void(Str title, std::wstring detail, bool error)> toast;
    };
    IconFilesView(AppState& state, IconPatchController& controller, const Localization& strings, Intents intents);
    ~IconFilesView() override;

    void refresh(); // queue / mount changed
    void selectFile(std::wstring_view name); // demo / tests: "imageres.dll"

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    class FileList;
    class IconGrid;

    void pick();
    void revertSelected();
    void saveSelected();
    void updateButtons();
    [[nodiscard]] const IconPatchController::Group* selectedGroup() const;

    AppState& m_state;
    IconPatchController& m_controller;
    const Localization& m_strings;
    Intents m_intents;
    std::wstring m_file; // relative path of the selected file
    ui::SearchBox* m_search = nullptr;
    FileList* m_list = nullptr;
    IconGrid* m_grid = nullptr;
    ui::Button* m_replace = nullptr;
    ui::Button* m_revert = nullptr;
    ui::Button* m_save = nullptr;
    ui::Button* m_restore = nullptr;
    float m_headerRight = 0; // where the buttons start (the header text stops there)
};

} // namespace wl::app
