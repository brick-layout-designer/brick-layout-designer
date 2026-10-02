#pragma once

#include <QDockWidget>
#include <QString>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QTouchEvent;

namespace bld::ui {

// MIME type used when dragging a module from the library panel onto the map.
// The payload is the module .bbm's absolute path (UTF-8).
inline constexpr const char* kModuleDragMimeType = "application/x-bld-module-path";

// A simple library of module .bbm files on disk. Backed by a QSettings folder
// path ("modules/libraryPath"). Lists every .bbm in that folder; double-click
// (or the right-click "Import into map" action) emits moduleImportRequested
// with the full file path. MainWindow turns that into the existing
// ImportBbmAsModuleCommand flow, so saving a module here and loading it into
// another project is symmetric. The list is also drag-enabled so the user
// can drop a module onto the map at a specific cursor position.
class ModuleLibraryPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit ModuleLibraryPanel(QWidget* parent = nullptr);

    QString libraryPath() const;
    void setLibraryPath(const QString& dir);
    void refresh();
    QListWidget* list() const { return list_; }

signals:
    void moduleImportRequested(const QString& bbmPath);
    // By touch, a finger slid sideways off a module carries it (screen
    // coordinates) until it lifts or the touch is cancelled; MainWindow
    // hands these to the map. Up and down scrolls the list instead.
    void touchDragMoved(const QString& bbmPath, QPoint globalPos);
    void touchDragDropped(const QString& bbmPath, QPoint globalPos);
    void touchDragCancelled();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onChooseFolder();
    void onActivated(QListWidgetItem* item);

private:
    QLabel*      header_ = nullptr;
    QListWidget* list_   = nullptr;
    QString      path_;
    enum class TouchState { None, Undecided, Scroll, Drag };
    TouchState touch_ = TouchState::None;
    QPointF touchStart_;
    QString touchPath_;
    bool handleTouch(QTouchEvent* e);
};

}
