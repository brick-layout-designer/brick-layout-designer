#pragma once

// Save to library… for a placed module when a server is signed in, as the
// web's SaveToLibraryDialog: a new library module, or a new version of one
// you can change, with a "What changed?" note. A new one is saved to you or one
// of your clubs ("Save to"); "This computer" keeps it in the Module library
// folder as before. The server decides who may save where (members of some
// clubs can't add modules); its answer is shown as it comes.

#include "../sync/LibraryApi.h"

#include <QDialog>

#include <QList>
#include <QStringList>
#include <QString>
#include <functional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;

namespace bld::ui {

class ServerLibrary;

class SaveModuleDialog : public QDialog {
    Q_OBJECT
public:
    struct Choice {
        bool onThisComputer = false;
        QString title;     // a new module's name
        QString orgSlug;   // a new module's club; empty: you
        QString updateId;  // the module a new version is saved over; empty: a new module
        QString note;      // "What changed?"
        bool oneSheet = false;  // "Put everything on one sheet"
    };

    // `defaultOwner`: the club slug to offer first (the layout's), else you.
    SaveModuleDialog(ServerLibrary& library, const QString& defaultOwner, QWidget* parent = nullptr);

    Choice choice() const;
    // The placed module being saved: the title says it, and its name is the new module's.
    void setModuleName(const QString& name);
    // The module's sheets: with two or more, the dialog says "This module
    // uses 2 sheets: Track, Buildings" and offers "Put everything on one
    // sheet" (named `oneSheetName`).
    void setSheets(const QStringList& names, const QString& oneSheetName);
    // Fill in and press Save (tests).
    QLineEdit* nameEdit() const { return name_; }
    QComboBox* saveTo() const { return saveTo_; }
    QRadioButton* updateButton() const { return update_; }
    QListWidget* targets() const { return targets_; }
    QLineEdit* noteEdit() const { return note_; }
    QLabel* error() const { return error_; }
    QWidget* sheetsBox() const { return sheetsBox_; }
    QLabel* sheetsLabel() const { return sheetsLabel_; }
    QCheckBox* oneSheetBox() const { return oneSheet_; }

    // The "Save to" entry that keeps the module on this computer.
    static constexpr const char* kThisComputer = "\x01local";

    // Save: checks the name (or the module picked) first, and asks before a
    // new module goes straight into a club (the club will own it).
    void accept() override;
    // How it asks about a club (tests answer instead).
    std::function<bool(const QString& club)> confirmSaveToClub;

private:
    void showMode();

    ServerLibrary& library_;
    QRadioButton* new_ = nullptr;
    QRadioButton* update_ = nullptr;
    QWidget* newPage_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* saveTo_ = nullptr;
    QListWidget* targets_ = nullptr;
    QLineEdit* note_ = nullptr;
    QLabel* error_ = nullptr;
    QWidget* sheetsBox_ = nullptr;
    QLabel* sheetsLabel_ = nullptr;
    QCheckBox* oneSheet_ = nullptr;
    QStringList sheets_;
    QString oneSheetName_;
    void showSheets();
};

}  // namespace bld::ui
