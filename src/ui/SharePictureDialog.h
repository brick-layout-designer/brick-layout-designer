#pragma once

// "Share Picture…" (the web's SharePictureDialog.tsx): a picture of the
// layout, the whole thing, what's on screen or a saved view, saved as a
// PNG or copied to the clipboard. Sensible defaults and no questions;
// "More options…" opens the detailed Export as Image dialog. Below it,
// "Export all views" writes one picture per saved view into a folder,
// remembering the folder and the size so doing it again is one click.

#include "SavedViews.h"

#include <QDialog>
#include <QImage>
#include <QString>

#include <memory>
#include <optional>

class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

// Export all views' remembered choices (QSettings views/exportFolder and
// views/exportScale).
struct ExportViewsPrefs {
    QString folder;
    double scale = 2.0;
};
ExportViewsPrefs loadExportViewsPrefs();
void saveExportViewsPrefs(const ExportViewsPrefs& prefs);

// Export all views into the remembered folder at the remembered size,
// asking for a folder first when `chooseFolder` or none is remembered yet.
// Returns what to tell the user, or empty when they cancelled.
QString runExportAllViews(QWidget* parent, const core::Map& map, parts::PartsLibrary& parts,
                          const QString& layoutTitle, bool chooseFolder);

class SharePictureDialog : public QDialog {
    Q_OBJECT
public:
    // The choice for what's on screen now; the whole layout's is
    // views::wholeLayout().id; anything else is a saved view's id.
    static QString screenChoice() { return QStringLiteral("screen"); }

    struct Input {
        const core::Map* map = nullptr;
        parts::PartsLibrary* parts = nullptr;
        QString layoutTitle;
        // What's on screen now; unset leaves that choice out.
        std::optional<views::PictureSpec> screen;
        // The picture picked when it opens; empty: the whole layout.
        QString initialChoice;
    };
    SharePictureDialog(Input input, QWidget* parent = nullptr);
    ~SharePictureDialog() override;

    // Picks what the picture shows and makes it.
    void choose(const QString& choice);
    QString choice() const;
    // The picture made for the current choice (null when there is nothing to show).
    QImage picture() const { return picture_; }
    QString fileName() const;
    // Writes the picture as a PNG; false (and the status says why) when it can't.
    bool savePictureTo(const QString& path);
    void copyPicture();
    QString status() const;

    // Export all views at the picked size into `folder`, remembering both.
    views::ExportAllResult exportAllTo(const QString& folder);
    double exportScale() const;
    void setExportScale(double scale);

signals:
    // "More options…": the caller opens Export as Image (the dialog closes).
    void moreOptionsRequested();

private:
    void refreshFolder();
    Input in_;
    std::unique_ptr<views::PictureRenderer> renderer_;
    QImage picture_;
    QComboBox* choiceBox_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* size_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    QButtonGroup* scaleGroup_ = nullptr;
    QLabel* folder_ = nullptr;
};

}  // namespace bld::ui
