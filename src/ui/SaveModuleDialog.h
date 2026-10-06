#pragma once

// "Save Selection as Module" when a server is signed in,
// as the web's SaveModuleDialog: a new module, or a new version of one you
// can change, with a "What changed?" note. A new one is saved to you or one
// of your clubs ("Save to"); "This computer" keeps it in the Module library
// folder as before. The server decides who may save where (members of some
// clubs can't add modules); its answer is shown as it comes.

#include "../sync/LibraryApi.h"

#include <QDialog>

#include <QList>
#include <QString>
#include <functional>

class QButtonGroup;
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
    };

    // `defaultOwner`: the club slug to offer first (the layout's), else you.
    SaveModuleDialog(ServerLibrary& library, const QString& defaultOwner, QWidget* parent = nullptr);

    Choice choice() const;
    // Fill in and press Save (tests).
    QLineEdit* nameEdit() const { return name_; }
    QComboBox* saveTo() const { return saveTo_; }
    QRadioButton* updateButton() const { return update_; }
    QListWidget* targets() const { return targets_; }
    QLineEdit* noteEdit() const { return note_; }
    QLabel* error() const { return error_; }

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
};

}  // namespace bld::ui
