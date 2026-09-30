#pragma once
// D-051 Dosyalar: files and folders of this PC that go into the image. A 56px drop zone (click =
// "Dosya ekle…"), then Kaynak · İmajdaki yer · Boyut · Risk. Delete removes the selected row.
// Header actions (Shell): "Dosya ekle…", "Klasör ekle…"; both ask where in the image.
#include "app/Localization.h"
#include "app/controllers/FilesController.h"
#include "ui/widgets/DropZone.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class FilesPage : public ui::Widget {
public:
    FilesPage(AppState& state, FilesController& controller, const Localization& strings, Language language,
              std::function<void()> addFiles, std::function<void()> goImages);
    ~FilesPage() override;

    void setDragState(ui::DropZone::DragState state);
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    FilesController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<core::ops::Operation> m_rows;
    ui::DropZone* m_drop = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
