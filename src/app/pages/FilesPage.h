#pragma once
// D-051 Dosyalar: files and folders of this PC that go into the image. A 56px drop zone (click =
// "Dosya ekle…"), then Kaynak · marks · İmajdaki yer · Boyut. Delete removes the selected row.
// Header actions (Shell): "Dosya ekle…", "Klasör ekle…"; both ask where in the image.
// 2c (after D-139): the Risk column's word became the risk's dot in a marks column after the source
// (the word and why in its tooltip and the card); the selected item in the detail card (the whole
// source path the table cuts, where it lands on the installed Windows, its size and kind, the risk's
// reason, "Kuyruktan çıkar" = Delete).
#include "app/Localization.h"
#include "app/controllers/FilesController.h"
#include "app/pages/DetailCard.h"
#include "ui/widgets/DropZone.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wl::app {

class FilesPage : public ui::Widget, public DetailSource {
public:
    FilesPage(AppState& state, FilesController& controller, const Localization& strings, Language language,
              std::function<void()> addFiles, std::function<void()> goImages);
    ~FilesPage() override;

    void setDragState(ui::DropZone::DragState state);

    // The detail card (2c).
    enum Action : int { kActionRemove = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;
    void selectRowForTests(int row) { m_table->setSelected(row); }

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void removeRow(int row); // Delete / the card's button
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    FilesController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<core::ops::Operation> m_rows;
    // Audit PG-07: "is the source a folder" was asked of the file system for every visible row on
    // every frame (a source on a share that is gone: a network timeout per frame). Asked once per
    // source, when it first shows up in the queue.
    std::map<std::wstring, bool> m_folders; // source path -> it is a folder
    std::vector<bool> m_rowIsFolder;        // one per m_rows
    class SummaryLine* m_summaryLine = nullptr; // D-139: "n öğe · imaja x eklenir" at the header row's right
    float m_titleY = 0;                         // the header row's top (layout)
    ui::DropZone* m_drop = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
