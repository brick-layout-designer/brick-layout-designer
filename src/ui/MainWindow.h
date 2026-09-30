#pragma once

#include <QMainWindow>
#include <QString>
#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>

#ifdef BLD_SYNC
#include "PartsUpload.h"
#include <QSet>
#endif

class QAction;
class QLabel;
class QMenu;
class QComboBox;

namespace bld::core { struct Venue; class Map; }
namespace bld::sync {
struct ConnectResult;
namespace merge { struct Snapshot; }
}
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class MapView;
class LayerPanel;
class PartsBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(parts::PartsLibrary& parts, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);

    // Seed a blank document if none is loaded. Call once after the
    // startup file-load attempts so the user always opens to a working
    // canvas — without it, currentMap() stays null until File > Open
    // and silent-fail bugs (e.g. add-layer doing nothing) appear.
    void ensureDocument();

protected:
    void closeEvent(QCloseEvent* e) override;
    // Redraws the toolbar's icons when the theme (palette) changes.
    void changeEvent(QEvent* e) override;

    // Rescans the parts library against all configured paths. Called at
    // startup and after the user edits the paths via onManageLibraries.
    void rescanLibrary(const QStringList& paths);

    // Load/save the user's custom library paths to QSettings. The vendored
    // submodule path is always prepended regardless of user state.
    QStringList loadUserLibraryPaths() const;
    void saveUserLibraryPaths(const QStringList& paths);

    QString defaultVendoredPartsRoot() const;

    // Path where Tools → Import lands rendered library parts. Computed
    // the same way the importer does so the parts panel finds them on
    // every launch — modules library + "/imports" if configured,
    // otherwise AppDataLocation/imports as the universal fallback.
    QString importedPartsRoot() const;

    // Register a freshly-written library part with the in-memory
    // parts library and the parts panel without re-scanning every
    // configured search path or rebuilding the whole grid (which
    // freezes the UI for thousands-of-parts libraries). Returns the
    // library key on success, or an empty string if the file isn't
    // a valid part XML or is already indexed.
    QString registerImportedPart(const QString& xmlAbsPath);

    // Import an LDraw / Studio / LDD file as a new library part: prepare,
    // preview (rotate, connections, name, category), write, register and
    // place it at the view centre.
    void importModelFile(const QString& path);

    // Re-run the import of an imported part from its recorded source file
    // (with the same rotation and removed connection points), through the
    // preview when `interactive`, replacing the part in place.
    bool reimportPart(const QString& key, bool interactive);
    // Tools > Import > Re-import Changed Parts: every imported part whose
    // source file changed since it was imported.
    void onReimportChangedParts();

private slots:
    void onNew();
    void onOpen();
    bool onSave();
    bool onSaveAs();
    void onZoomIn();
    void onZoomOut();
    void onFitToView();
    void onManageLibraries();
    void onReloadLibrary();
    void onBatchImport();
    void onExportPartList();
    void onAbout();
    void onCreateModuleFromSelection();
    void onImportBbmAsModule();
    void onSaveSelectionAsModule();
    void onSaveSelectionAsSet();
    void onImportModuleFromLibraryPath(const QString& bbmPath);
    void rebuildRecentMenu();
    void pushRecentFile(const QString& path);

private:
    void setupMenus();
    // The friendlier shell (MainWindowShell.cpp): labelled toolbar, task
    // tabs, panel headers and the status bar's piece count, sheet and zoom.
    void setupShell();
    void refreshShellIcons();
    void addZoomReadout();
    // Build / Room / Notes / Parts list: shortcuts onto the docks and dialogs.
    void showTask(const QString& task);
    // Edit > Settings...: the look and help settings.
    void openSettings();
    // The server the settings sync with now, or empty.
    QString syncedHost() const;
    void setupMapMenu();           // in MainWindowMapMenu.cpp
    void setupToolsMenu();         // in MainWindowToolsMenu.cpp
    void setupBudgetMenu();        // in MainWindowBudgetMenu.cpp
    bool maybeSaveBudget();        // prompts on an unsaved budget
    bool saveBudget(bool askForName);
    void updateTitle();
    bool maybeSave();              // prompts on dirty close
    bool newDocument();            // File > New; false when the user cancels the save prompt
    // Venue Library "Start Layout": a new layout (after the usual unsaved-
    // changes prompt) with that venue in place.
    void startLayoutFromVenue(const core::Venue& venue);
    bool writeMapTo(const QString& path);
    // Puts a just-read map in the window as the document at `path`.
    void showLoadedMap(std::unique_ptr<core::Map> map, const QString& path, const QStringList& warnings);
    // Parts a .bld-layout carries that the library lacks: written to the
    // layout-parts folder, which joins the library paths. Returns what to
    // tell the user, and the parts the library has with other definitions.
    struct LayoutPartsTaken {
        QStringList notes;
        QStringList differing;  // part keys
    };
    LayoutPartsTaken takeInLayoutParts(const QMap<QString, QByteArray>& files);
    // Shows the differing parts side by side and applies what the user
    // picks: the layout's replaces yours (backed up first), or is added under
    // a new number that `map`'s bricks switch to. Returns what to tell the user.
    QStringList resolvePartDifferences(const QStringList& keys, const QMap<QString, QByteArray>& files,
                                       core::Map& map);
    QString layoutPartsFolder();  // AppDataLocation/layout-parts, kept in the library paths
    // The parts to put in a .bld-layout: the ones outside the bundled library.
    QMap<QString, QByteArray> partsToEmbed() const;
    // Save on a file that isn't a .bld-layout asks once whether to switch.
    bool chooseSaveFormat();
    bool saveFormatChosen_ = false;
    void onExportBbm();

    parts::PartsLibrary& parts_;
    MapView*      mapView_     = nullptr;
    LayerPanel*   layerPanel_  = nullptr;
    PartsBrowser* partsBrowser_ = nullptr;
    class ModulesPanel* modulesPanel_ = nullptr;
    class ModuleLibraryPanel* moduleLibraryPanel_ = nullptr;
    class VenueLibraryPanel* venueLibraryPanel_ = nullptr;
    class PartUsagePanel* partUsagePanel_ = nullptr;
    class BudgetSession* budget_ = nullptr;
    class UpdateCheck* updates_ = nullptr;

    QString currentFilePath_;
    int     cleanUndoIndex_ = 0;   // index at which the stack is "clean"
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;

    class QMenu* recentMenu_ = nullptr;
    QAction* generalInfoAct_ = nullptr;   // Map > General Info (the Notes tab)
    QAction* preferencesAct_ = nullptr;   // Edit > Preferences (Settings' "More options")
    QAction* helpContentsAct_ = nullptr;
    class QToolBar* mainToolbar_ = nullptr;
    QList<QPair<QAction*, QString>> shellIcons_;  // toolbar actions and their icon names
    std::function<void()> refreshPaintSwatch_;

#ifdef BLD_SYNC
    // Live layouts on a collaborative server (MainWindowLive.cpp).
    void setupLiveMenu(QMenu* file);
    void onConnectToServer();
    void onDisconnect();
    void onDownloadVenues();
    void onPublishToServer();
protected:
    // Open a server layout live in this window. Protected so tests can open
    // one without the connect dialog.
    void openLive(const bld::sync::ConnectResult& r);
private:
    // Offer to upload the user's parts the live server lacks; `quiet`: say
    // nothing when there are none (after publishing).
    void offerPartsUpload(bool quiet);
    QUrl liveServer_;
    QString liveToken_;
    // Settings sync with the connected server's account.
    class PrefsSync* prefsSync_ = nullptr;
    QAction* uploadPartsAct_ = nullptr;
    // Your own parts you place while live that the server lacks: each is
    // offered for upload once per session. The catalog is fetched on open
    // and after uploads, not per edit.
    void loadLivePartsCatalog();
    void offerPlacedParts();
    QList<bld::sync::LocalPart> liveLocalParts_;
    QSet<QString> liveServerParts_; // upper-cased keys and part numbers
    bool liveCatalogReady_ = false;
    QSet<QString> liveAskedParts_;  // offered already, or in the layout as opened
    bool liveLayoutSeen_ = false;
    bool liveOfferOpen_ = false;
    // Bring this server's parts into the library (in the background):
    // missing or changed ones are downloaded to a folder named after it.
    void syncServerParts(const QUrl& server, const QString& token);
    void onLiveReloaded();
    void updateLiveUi();
    // Back in step with offline edits: the compare window, then apply,
    // discard, replace the server's or save mine as a new layout.
    void onReviewOfflineEdits();
    // Select and centre the brick with this id, if the map shows it.
    void showBrick(const QString& guid);
    // Publish my offline version as a new layout of mine; the edits are
    // resolved (the live layout keeps the server's) once it is published.
    void saveOfflineAsNew(const bld::sync::merge::Snapshot& mine);
    QAction* reviewOfflineAct_ = nullptr;
    class LiveLayout* live_ = nullptr;
    QLabel* liveStatus_ = nullptr;
    QAction* disconnectAct_ = nullptr;
    QAction* liveUndoAct_ = nullptr;
    QAction* liveRedoAct_ = nullptr;
#endif

    // Auto-save: flushes the current map to a sidecar file every N seconds if
    // the undo stack is dirty. On startup, if an autosave file is newer than
    // the last-opened file, we offer to restore it (see restoreAutosaveIfAny).
    class QTimer* autosaveTimer_ = nullptr;
    void performAutosave();
    void performAutosaveThrottled();  // called on every undo-stack change
public:
    // Returns the path where the autosave file lives for the current session
    // (AppDataLocation/autosave.bld-layout). Public so main.cpp can check on startup.
    static QString autosavePath();
    // Offers to restore the autosave if it exists and is newer than lastFile.
    // Called from main.cpp before openFile()ing the recent file. Returns true
    // if the user accepted the restore (in which case main.cpp should skip
    // reopening lastFile).
    bool restoreAutosaveIfAny(const QString& lastFile);
};

}
