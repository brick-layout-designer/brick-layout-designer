#pragma once

// Make a module and Update library version, as the web's ModuleDialogs.tsx.
//   - Make a module: the picked parts become a module in this layout. "Also
//     save to my library" saves it there too, in one go (Save to, and the
//     sheets line with "Put everything on one sheet").
//   - Update library version: a linked module's parts become the library's
//     next version, with a "What changed?" note.
// Save to library… is SaveModuleDialog.

#include <QDialog>
#include <QStringList>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace bld::ui {

class ServerLibrary;

// "This module uses 2 sheets: …" and "Put everything on one sheet" (shown with two or more sheets).
class ModuleSheetsBox;

class MakeModuleDialog : public QDialog {
    Q_OBJECT
public:
    struct Choice {
        QString name;
        bool alsoSave = false;
        bool onThisComputer = false;  // saved to the Module library folder
        QString orgSlug;              // a club; empty: you
        bool oneSheet = false;
    };
    // `library`: the signed-in server's library (null or not ready: only
    // this computer); `defaultOwner`: the club slug to offer first.
    MakeModuleDialog(ServerLibrary* library, const QString& defaultOwner, int partCount, QWidget* parent = nullptr);

    void setSheets(const QStringList& names, const QString& oneSheetName);
    Choice choice() const;
    void accept() override;
    // How it asks about a club (tests answer instead).
    std::function<bool(const QString& club)> confirmSaveToClub;

    QLineEdit* nameEdit() const { return name_; }
    QCheckBox* alsoSaveBox() const { return alsoSave_; }
    QComboBox* saveTo() const { return saveTo_; }
    QCheckBox* oneSheetBox() const;
    QLabel* error() const { return error_; }
    QPushButton* makeButton() const { return make_; }

private:
    void showSave();
    QLineEdit* name_ = nullptr;
    QCheckBox* alsoSave_ = nullptr;
    QWidget* saveBox_ = nullptr;
    QComboBox* saveTo_ = nullptr;
    ModuleSheetsBox* sheets_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* make_ = nullptr;
};

class PublishModuleDialog : public QDialog {
    Q_OBJECT
public:
    // `version`: the version this layout's copy matches (0: not known);
    // `latest`: the library's newest; `canPublish`: you may save versions of it.
    PublishModuleDialog(const QString& libraryTitle, int version, int latest, bool canPublish, QWidget* parent = nullptr);
    void setSheets(const QStringList& names, const QString& oneSheetName);
    QString note() const;
    bool oneSheet() const;

    QLabel* text() const { return text_; }
    QLabel* newerWarning() const { return newer_; }
    QLineEdit* noteEdit() const { return note_; }
    QPushButton* saveButton() const { return save_; }

private:
    QLabel* text_ = nullptr;
    QLabel* newer_ = nullptr;
    QLineEdit* note_ = nullptr;
    ModuleSheetsBox* sheets_ = nullptr;
    QPushButton* save_ = nullptr;
};

}  // namespace bld::ui
