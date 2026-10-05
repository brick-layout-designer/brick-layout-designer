#pragma once

// The Views panel (the web's ViewsPanel.tsx): the layout's saved views.
// Click a view to see it (only this screen changes, not the layout) and
// click it again to stop. Each row shares a picture of its view, and its
// "Edit" opens the view's settings under it: "Fit whole layout" or "Use
// this area", its sheets, grid and labels, rename and delete, and "Done"
// closes them. Every change goes out as viewsEdited with the whole new
// list, which the main window applies as one undoable step.

#include "../core/SavedView.h"

#include <QDockWidget>
#include <QFrame>
#include <QRectF>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace bld::core { class Map; }

namespace bld::ui {

class ViewsPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit ViewsPanel(QWidget* parent = nullptr);

    // The layout whose views it lists (null: none). Call again after any change.
    void setMap(const core::Map* map);
    // The view being looked at, marked "On screen now" in the list; empty for none.
    void setActiveView(const QString& id);
    QString activeViewId() const { return activeId_; }

    // The map area on screen now, in studs ("Use this area" keeps it).
    void setScreenRect(std::function<std::optional<QRectF>()> f) { screenRect_ = std::move(f); }
    // Whether the grid shows now (a new view starts with the same).
    void setGridShown(std::function<bool()> f) { gridShown_ = std::move(f); }
    // Asks before deleting; the default is a Yes / No message box.
    void setConfirm(std::function<bool(const QString& name)> f) { confirm_ = std::move(f); }
    // Asks for a name (Add view, Rename); the default is an input dialog.
    // Unset means cancelled.
    void setAskName(std::function<std::optional<QString>(const QString& title, const QString& name)> f) {
        askName_ = std::move(f);
    }

    // What the buttons do, callable directly.
    void addView(const QString& name);
    void renameView(const QString& id, const QString& name);
    // Asks first; true when it was deleted.
    bool deleteView(const QString& id);
    // "Fit whole layout" (true) or "Use this area" (false: keeps what's on screen now).
    void setFit(const QString& id, bool fit);
    void setGrid(const QString& id, bool on);
    void setLabels(const QString& id, bool on);
    // Unset: every sheet as the layout has it.
    void setSheets(const QString& id, std::optional<QStringList> sheets);

    // The view whose settings are open under its row ("Edit"), or empty.
    QString openViewId() const { return openId_; }
    // Opens a view's settings (closing any other's); empty closes them ("Done").
    void openView(const QString& id);

signals:
    void viewsEdited(const std::vector<core::SavedView>& views, const QString& what);
    // A view's row was clicked: show it.
    void goToViewRequested(const core::SavedView& view);
    // The row of the view on show was clicked again: stop showing it,
    // leaving the map where it is.
    void leaveViewRequested();
    void showEverythingRequested();
    void sharePictureRequested(const QString& viewId);
    void exportAllRequested();

private:
    const core::SavedView* find(const QString& id) const;
    // Edits one view and sends the new list.
    void edit(const QString& id, const QString& what, const std::function<void(core::SavedView&)>& change);
    void rebuildList();
    void refreshOptions();
    std::vector<std::pair<QString, QString>> sheetList() const;  // non-grid layers: id, name

    const core::Map* map_ = nullptr;
    std::vector<core::SavedView> views_;
    QString activeId_;
    QString openId_;
    bool updating_ = false;

    std::function<std::optional<QRectF>()> screenRect_;
    std::function<bool()> gridShown_;
    std::function<bool(const QString&)> confirm_;
    std::function<std::optional<QString>(const QString&, const QString&)> askName_;

    QLabel* empty_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* rowsHost_ = nullptr;
    QVBoxLayout* rowsCol_ = nullptr;
    std::vector<QFrame*> cards_;  // one per view, in order
    QFrame* options_ = nullptr;   // in the open view's card, else hidden
    QPushButton* fitBtn_ = nullptr;
    QPushButton* areaBtn_ = nullptr;
    QLabel* areaHint_ = nullptr;
    QCheckBox* gridChk_ = nullptr;
    QCheckBox* labelsChk_ = nullptr;
    QCheckBox* allSheetsChk_ = nullptr;
    QWidget* sheetsBox_ = nullptr;
    QVBoxLayout* sheetsCol_ = nullptr;
    QPushButton* showAllBtn_ = nullptr;
    QPushButton* exportBtn_ = nullptr;
};

// "Showing Station · Show everything" over the map while a saved view is
// shown, like the web's. It keeps itself centred at the top of its parent.
class ViewIndicator : public QFrame {
    Q_OBJECT
public:
    explicit ViewIndicator(QWidget* parent);
    // Shows "Showing <name>"; an empty name hides it.
    void setViewName(const QString& name);
    QString text() const;

signals:
    void showEverythingRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();
    QLabel* label_ = nullptr;
};

}  // namespace bld::ui
