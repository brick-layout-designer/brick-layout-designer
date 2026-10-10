#pragma once

#include "ImportPipeline.h"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <functional>

class QCheckBox;
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
// part, drop connection points they don't want, name it, pick the
// category (library sub-folder) it goes into and choose whether to
// replace an existing part of the same name. Cancel leaves the library
// untouched.
class ImportPreviewDialog : public QDialog {
    Q_OBJECT
public:
    // `categories` fills the category picker (editable, so new ones can be
    // typed). `partExists(name, category)` reports whether saving would
    // collide with an existing part, to offer "replace".
    ImportPreviewDialog(PreparedPart part,
                        const QStringList& categories,
                        const QString& defaultCategory,
                        std::function<bool(const QString&, const QString&)> partExists,
                        QWidget* parent = nullptr);

    // Re-importing an existing part: its name and category, replacing it.
    void presetForReimport(const QString& name, const QString& category);
    // ...and the stud alignment it was imported with.
    void presetAlignment(ImportAlign align, QPointF nudgeStuds);

    // Final values after exec() returns Accepted.
    PreparedPart result() const;   // rotated, unchecked connections removed
    QString partName() const;
    QString category() const;
    bool    replaceExisting() const;

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
    void refreshReplace();

    PreparedPart part_;
    PreparedPart base_;            // as prepared (and rotated), before alignment
    ImportAlign  align_ = ImportAlign::Automatic;
    QPointF      nudge_;
    class QComboBox* alignBox_ = nullptr;
    class QComboBox* stepBox_ = nullptr;
    std::function<bool(const QString&, const QString&)> partExists_;
    QGraphicsScene*      scene_       = nullptr;
    PreviewView*         view_        = nullptr;
    QGraphicsPixmapItem* pixmapItem_  = nullptr;
    QLabel*              header_      = nullptr;
    QListWidget*         connList_    = nullptr;
    QLineEdit*           nameEdit_    = nullptr;
    QLineEdit*           designerEdit_    = nullptr;
    QLineEdit*           designerUrlEdit_ = nullptr;
    QComboBox*           categoryBox_ = nullptr;
    QCheckBox*           replaceBox_  = nullptr;
};

}  // namespace bld::ui
