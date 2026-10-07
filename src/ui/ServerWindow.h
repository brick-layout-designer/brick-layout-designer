#pragma once

// File › Open from Server… (Ctrl+Shift+O, the toolbar's Server button, the
// status bar's server line, the welcome's "Open from server"): everything
// on a server in one window, laid out like the web's Home page.
//
// - The server you're looking at (one of yours) and who is signed in there.
// - Show: All, Mine or one club, remembered for each server.
// - Tabs: Layouts, Venues, Modules and Parts (yours and your clubs'), then
//   the server's Catalog and its Collections.
// - Each row: its picture, name, "by Sam · in ArkLUG", size and when it
//   last changed, with the web's actions: Open live / Download a copy /
//   Delete… for a layout, Use this venue / Add to my venue library /
//   Delete… for a venue, Insert into this layout / Open to change it /
//   Save a copy locally / Delete… for a module, Add to my parts for a part.
//   A club's thing can be taken back by its author or given back to them.
// - Not signed in, offline or refused: a friendly line and the one button
//   that helps (Sign in, Add a server…, Try again), never an empty window.
//
// It shows what the app's ServerLibrary shows (the same server, modules,
// clubs, catalog and picture cache as the Module library's tabs, so a
// picture the server hasn't got is asked for once). The lists follow the
// server's live hints and are asked for again when you come back to the
// window. MainWindow does what needs the layout (opening, inserting,
// starting a layout in a venue, the parts library).

#include "../sync/LibraryApi.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>
#include <QWidget>

#include <functional>

class QComboBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QTimer;
class QVBoxLayout;

namespace bld::ui {

class CatalogTab;
class LibraryStatusLine;
class ServerLibrary;
struct ConfirmOptions;

class ServerWindow : public QWidget {
    Q_OBJECT
public:
    enum class Tab { Layouts, Venues, Modules, Parts, Catalog, Collections };

    // Something on this computer that isn't on the server yet: the open
    // layout, a module placed in it or in the Module library folder, a venue
    // in the Venue library, one of your own parts. MainWindow lists them.
    struct LocalItem {
        QString id;       // MainWindow's key for it (a path, a module id, a part number)
        QString title;
        QString detail;   // where it is ("In this layout", "Module library folder")
    };

    ServerWindow(ServerLibrary& library, QWidget* parent = nullptr);

    // Shows the window on that tab, in front.
    void showTab(Tab tab);
    Tab currentTab() const;

    // Your servers again (after Servers… changed them), with the library's picked.
    void refreshServers();
    // Asks the server for the layouts, venues and parts again (while the
    // window is open; else when it next opens).
    void reload();
    // A live hint from the server (GET /api/events): what changed is asked for again.
    void hint(const QJsonObject& hint);
    // The parts library changed (a part was added): the Parts rows say so.
    void partsChanged();

    // What's on this computer only, for one tab ("Not on the server yet",
    // with Send…). One with the name of something on the server isn't shown.
    void setLocalItems(Tab tab, const QList<LocalItem>& items);
    // The local items that show (tests).
    QStringList localShown(Tab tab) const;

    // The Show filter's value for a server ("all", "me" or a club's slug), as remembered.
    static QString rememberedShow(const QUrl& server);

    // For tests: the controls, a row by its kind and id ("layoutRow:<id>",
    // "venueRow:<id>", "moduleRow:<id>", "partRow:<id>"), the empty line.
    QComboBox* serverPicker() const { return servers_; }
    QComboBox* showFilter() const { return show_; }
    QLineEdit* search() const { return search_; }
    QTabWidget* tabs() const { return tabs_; }
    LibraryStatusLine* statusLine() const { return status_; }
    QLabel* account() const { return account_; }
    QWidget* row(Tab tab, const QString& id) const;
    QLabel* emptyLine(Tab tab) const;
    CatalogTab* catalogTab() const { return catalog_; }
    CatalogTab* collectionsTab() const { return collections_; }
    // How many times the lists were asked for (tests).
    int loads() const { return loads_; }

    // How it asks before deleting or giving back (tests answer instead).
    std::function<bool(const ConfirmOptions&)> confirm;
    // Where Download a copy saves: a path ending .bld-layout or .bbm, or
    // empty when cancelled. `native`: the server can send a .bld-layout.
    std::function<QString(const QString& title, bool native)> askSavePath;
    // Whether the parts library has this part number (MainWindow's).
    std::function<bool(const QString& partNumber)> hasPart;

signals:
    // Another of your servers was picked.
    void serverPicked(const QUrl& server);
    void manageServersRequested();
    // The rows' actions MainWindow carries out.
    void openLayoutRequested(const bld::sync::LayoutEntry& layout);
    void useVenueRequested(const QString& venueId, const QString& name);
    void addVenueRequested(const QString& venueId, const QString& name);
    void saveModuleCopyRequested(const QString& moduleId);
    void addPartRequested(const QString& partNumber);
    void showPartRequested(const QString& partNumber);
    // Sending: something local to the server, a layout file, all your
    // parts the server lacks, and sharing a server thing to the catalog.
    void sendRequested(bld::ui::ServerWindow::Tab tab, const QString& localId);
    void saveLayoutFileRequested();
    void uploadPartsRequested();
    // The window opened or came back to the front: what's local may have changed.
    void localRefreshWanted();
    void shareRequested(const bld::sync::CatalogShare& share, bool update, const QString& clubReview);
    // Something to say in the status bar.
    void message(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    struct Page {
        QScrollArea* scroll = nullptr;
        QWidget* host = nullptr;
        QVBoxLayout* list = nullptr;
        QLabel* empty = nullptr;
        QHash<QString, QWidget*> rows;
        QList<LocalItem> local;
    };
    struct Action {
        QString text;
        QString name;            // the object name (tests find it by this)
        std::function<void()> run;
        bool enabled = true;
        QString tip;
    };
    struct RowSpec {
        QString objectName;
        QString title;
        QStringList lines;       // muted lines under the title
        QString picture;         // a path on the server, or empty
        QString placeholder;     // shown until (or instead of) the picture
        Action primary;
        QList<Action> more;      // the ⋯ menu
        QString pill;            // "Editing now" instead of the buttons
    };

    Page makePage(const QString& name, const QList<QPushButton*>& tools = {});
    Page& page(Tab tab);
    // The "On this computer" rows of a tab, under the server's (`serverTitles` hides ones already there).
    void addLocalRows(Tab tab, const QStringList& serverTitles);
    // "Share to the public catalog…" / "Publish this update…" for a server
    // thing, when the server takes shares and you may share it; and its
    // catalog state for the row ("In the catalog: Public").
    void addShareAction(RowSpec& r, const QString& kind, const QString& sourceId, const QString& title, bool mayShare);
    QString catalogLine(const QString& kind, const QString& sourceId) const;
    bool hasFeature(const QString& feature) const;
    QWidget* makeRow(Page& page, const RowSpec& spec);
    void clearPage(Page& page);
    void showState();
    // Fetches the lists a hint (or reload) asked for.
    void loadLists();
    void rebuildFilter();
    void rebuild();
    void rebuildLayouts();
    void rebuildVenues();
    void rebuildModules();
    void rebuildParts();
    bool shown(const QString& ownerKey, const QStringList& texts) const;
    QString whereText(const QString& has, const QString& have) const;
    QString showKey() const;
    bool canDownloadNative() const;

    void deleteLayout(const sync::LayoutEntry& l);
    void downloadLayout(const sync::LayoutEntry& l);
    void deleteVenue(const sync::VenueEntry& v);
    void deleteModule(const sync::ServerModule& m);
    // A club's layout, venue, module or part back to its author (`kindPath` "layouts", …).
    void giveOrTakeBack(const QString& kindPath, const QString& word, const QString& id, const QString& title,
                        const sync::Credit& credit, bool give);
    QList<Action> returnActions(const QString& kindPath, const QString& word, const QString& id, const QString& title,
                                const sync::Credit& credit);
    bool ask(const ConfirmOptions& o);

    ServerLibrary& library_;
    QComboBox* servers_ = nullptr;
    QLabel* account_ = nullptr;
    LibraryStatusLine* status_ = nullptr;
    QWidget* content_ = nullptr;
    QWidget* filler_ = nullptr;
    QComboBox* show_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    Page layoutsPage_, venuesPage_, modulesPage_, partsPage_;
    CatalogTab* catalog_ = nullptr;
    CatalogTab* collections_ = nullptr;
    QTimer* hintTimer_ = nullptr;

    QList<sync::LayoutEntry> layouts_;
    QList<sync::VenueEntry> venues_;
    QList<sync::ServerPart> parts_;
    QList<sync::MyCatalogItem> shared_;  // yours and your clubs' in the catalog
    bool layoutsIn_ = false, venuesIn_ = false, partsIn_ = false;
    QString layoutsWhy_, venuesWhy_, partsWhy_;  // why a list couldn't be fetched
    bool stale_ = true;                          // the lists wait for the window to open
    int generation_ = 0;
    int loads_ = 0;
    QUrl shownServer_;
    // The kinds a hint asked for again, fetched together after a short wait.
    bool hintLayouts_ = false, hintVenues_ = false, hintParts_ = false, hintCatalog_ = false;
};

}  // namespace bld::ui
