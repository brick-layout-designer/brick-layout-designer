#pragma once

#include "ImportPipeline.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QComboBox;
class QGraphicsPixmapItem;
class QGraphicsScene;
class QLabel;
class QLineEdit;
class QListWidget;

namespace bld::ui {

class PreviewView;

// Preview shown after an LDraw / Studio / LDD model has been turned into
// a PreparedPart but BEFORE anything is written. The user can rotate the
// part, drop connection points they don't want, name it and pick the
// category (library sub-folder) it goes into. A name that's already taken
// is asked about after it (MainWindow: replace or keep both). Cancel
// leaves the library untouched.
class ImportPreviewDialog : public QDialog {
    Q_OBJECT
public:
    // `categories` fills the category picker (editable, so new ones can be
    // typed).
    ImportPreviewDialog(PreparedPart part,
                        const QStringList& categories,
                        const QString& defaultCategory,
                        QWidget* parent = nullptr);

    // Re-importing an existing part: its name and category.
    void presetForReimport(const QString& name, const QString& category);
    // ...and the stud alignment it was imported with.
    void presetAlignment(ImportAlign align, QPointF nudgeStuds);

    // Final values after exec() returns Accepted.
    PreparedPart result() const;   // rotated, unchecked connections removed
    QString partName() const;
    QString category() const;

private:
    void rotate(int quarterTurns);
    // Lay the part out again from base_ with the chosen alignment and nudge.
    void realign();
public:
    // Move the model against the stud grid (the buttons and arrow keys move
    // it by nudgeStep(), ½ to 1/16 stud). Tests use these directly.
    void nudge(QPointF studs);
    void setAlign(ImportAlign align);
    QPointF nudgeStuds() const { return nudge_; }
    double nudgeStep() const;
private:
    void refreshSprite();
    void refreshConnections();

    PreparedPart part_;
    PreparedPart base_;            // as prepared (and rotated), before alignment
    ImportAlign  align_ = ImportAlign::Automatic;
    QPointF      nudge_;
    class QComboBox* alignBox_ = nullptr;
    class QComboBox* stepBox_ = nullptr;
    QGraphicsScene*      scene_       = nullptr;
    PreviewView*         view_        = nullptr;
    QGraphicsPixmapItem* pixmapItem_  = nullptr;
    QLabel*              header_      = nullptr;
    QListWidget*         connList_    = nullptr;
    QLineEdit*           nameEdit_    = nullptr;
    QLineEdit*           designerEdit_    = nullptr;
    QLineEdit*           designerUrlEdit_ = nullptr;
    QComboBox*           categoryBox_ = nullptr;
};

}  // namespace bld::ui
