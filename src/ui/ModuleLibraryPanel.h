#pragma once

#include <QDockWidget>

#include <functional>
#include <QString>
#include <QStringList>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QTouchEvent;

class QPushButton;
class QTabWidget;

namespace bld::parts { class PartsLibrary; }

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
    // With the parts, each module shows its picture (drawn a few at a
    // time after the list fills, and cached until the file changes).
    void setParts(parts::PartsLibrary* parts);
    // Where the pictures are cached (tests); empty: the app's cache folder.
    void setThumbnailCacheDir(const QString& dir) { thumbCacheDir_ = dir; }
    // How many rows still wait for their picture.
    int pendingThumbnails() const { return static_cast<int>(pendingThumbs_.size()); }

    // "Browse the catalog on the web…" and "Your modules on the web…"
    // (builds that talk to a server).
    void setCatalogLinkVisible(bool visible);

    // The library's tabs: "On this computer" first; builds that talk to a
    // server add "On the server" and "Catalog" (ServerLibrary.h). The tab
    // bar shows once there is more than one.
    int addTab(QWidget* page, const QString& label);
    QTabWidget* tabs() const { return tabs_; }

    // Delete a module file from the folder (its sidecar too), after asking.
    // Returns whether it went.
    bool deleteModule(const QString& bbmPath);
    // How it asks (tests answer instead); gets the module's name.
    void setConfirm(std::function<bool(const QString& name)> f) { confirm_ = std::move(f); }

signals:
    void moduleImportRequested(const QString& bbmPath);
    // "Browse the catalog on the web…" was clicked.
    void browseCatalogRequested();
    // "Your modules on the web…" was clicked.
    void webModulesRequested();
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
    QTabWidget*  tabs_ = nullptr;
    QLabel*      header_ = nullptr;
    QPushButton* catalogLink_ = nullptr;
    QPushButton* webModulesLink_ = nullptr;
    QListWidget* list_   = nullptr;
    QString      path_;
    std::function<bool(const QString&)> confirm_;
    parts::PartsLibrary* parts_ = nullptr;
    QString thumbCacheDir_;
    QStringList pendingThumbs_;  // module paths whose row waits for its picture
    bool thumbScheduled_ = false;
    void scheduleThumbnails();
    void drawNextThumbnail();
    enum class TouchState { None, Undecided, Scroll, Drag };
    TouchState touch_ = TouchState::None;
    QPointF touchStart_;
    QString touchPath_;
    bool handleTouch(QTouchEvent* e);
};

}
