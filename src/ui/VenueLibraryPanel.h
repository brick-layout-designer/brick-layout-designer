#pragma once

#include "../core/Venue.h"

#include <QDockWidget>

#include <functional>
#include <QString>

#include <optional>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace bld::ui {

// Persistent dock panel listing all .bld-venue files in the library folder.
// Backed by QSettings key "venue/libraryPath" (same as the menu actions used
// before). Signals:
//   venueLoadRequested   — user wants to replace the project venue
//   venueSaveRequested   — user wants to save the current project venue
//                          (panel handles the save itself; this signal lets
//                           MainWindow forward the current venue)
class VenueLibraryPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit VenueLibraryPanel(QWidget* parent = nullptr);

    QString libraryPath() const;
    void setLibraryPath(const QString& dir);
    void refresh();

signals:
    // Emitted when the user activates an entry; caller applies it.
    void venueLoadRequested(const core::Venue& venue);
    // Emitted by "Start Layout": the caller starts a new layout with it.
    void newLayoutRequested(const core::Venue& venue);
    // Emitted when the user clicks "Save Current Venue"; caller must respond
    // by calling saveVenue() with the project's current venue.
    void venueSaveRequested();

public slots:
    // Called by MainWindow in response to venueSaveRequested; pops a name
    // dialog and writes the file.
    void saveVenue(const std::optional<core::Venue>& venue);

public:
    // Adds a downloaded .bld-venue file under `name` without replacing an
    // existing one ("Hall (2)" instead). Returns the file written, or an
    // empty string on failure.
    QString addVenueFile(const QString& name, const QByteArray& bytes);
    // "Save to Server…" for the picked venue (and in the Venue Designer):
    // shown once the app can send venues; `send` does the rest.
    void setSendToServer(std::function<void(const core::Venue&)> send);

private slots:
    void onChooseFolder();
    void onLoad();
    void onStartLayout();
    // Open the Venue Designer on a new venue, or on the selected one.
    void onNewVenue();
    void onDesign();
    void onDelete();
    void onRename();
    void onSelectionChanged();

private:
    void updateButtons();
    QString selectedPath() const;
    // Reads the selected venue file, warning (with `title`) when it can't.
    std::optional<core::Venue> readSelected(const QString& title);
    // Runs the designer; saves go to `path` (a new venue: a new file named after it).
    void design(core::Venue venue, QString path);

    QLabel*      pathLabel_  = nullptr;
    QListWidget* list_       = nullptr;
    QPushButton* loadBtn_    = nullptr;
    QPushButton* startBtn_   = nullptr;
    QPushButton* newBtn_ = nullptr;
    QPushButton* designBtn_ = nullptr;
    QPushButton* saveBtn_    = nullptr;
    QPushButton* deleteBtn_  = nullptr;
    QPushButton* renameBtn_  = nullptr;
    QLabel*      detailLabel_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    std::function<void(const core::Venue&)> sendToServer_;
    QString      path_;
};

}  // namespace bld::ui
