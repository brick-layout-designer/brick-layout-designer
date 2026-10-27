#pragma once

// Save selection as set: the set's name, a line saying what a set is (and
// its "?" help), and "Also put it on the server" when you're signed in to a
// server that takes sets, as the web's Save selection as set dialog. The set
// itself is written into your parts folder by MainWindow.

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace bld::ui {

class SaveSetDialog : public QDialog {
    Q_OBJECT
public:
    // `partCount`: the parts picked. `serverLabel`: the server the set can go
    // to as well (its name in "Your servers"); empty hides the checkbox.
    // `alsoOnServer`: whether the checkbox starts ticked.
    SaveSetDialog(int partCount, const QString& serverLabel, bool alsoOnServer, QWidget* parent = nullptr);

    // The name typed, trimmed ("New Set" when left empty).
    QString name() const;
    // Ticked, and shown (a server that takes sets).
    bool alsoOnServer() const;

    QLineEdit* nameEdit() const { return name_; }
    QCheckBox* serverBox() const { return server_; }
    QPushButton* saveButton() const { return save_; }

private:
    QLineEdit* name_ = nullptr;
    QCheckBox* server_ = nullptr;
    QPushButton* save_ = nullptr;
};

// Rename set…: the set's new name, and "Also rename it on <server>" once
// the set turns out to be on a server you're signed in to (it's looked for
// while the dialog is open).
class RenameSetDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenameSetDialog(const QString& current, QWidget* parent = nullptr);

    // The set is on `serverLabel` too, where you may rename it: the
    // checkbox shows, ticked.
    void showServerOption(const QString& serverLabel);
    // The name typed, trimmed.
    QString name() const;
    // Shown and ticked.
    bool alsoOnServer() const;

    QLineEdit* nameEdit() const { return name_; }
    QCheckBox* serverBox() const { return server_; }
    QPushButton* renameButton() const { return rename_; }

private:
    QLineEdit* name_ = nullptr;
    QCheckBox* server_ = nullptr;
    QPushButton* rename_ = nullptr;
};

// The server already has a part or set with this set's name: a free name
// to use instead (suggested, and editable). Tests drive it without exec().
class SetNameTakenDialog : public QDialog {
    Q_OBJECT
public:
    SetNameTakenDialog(const QString& taken, const QString& suggested, const QString& serverLabel,
                       QWidget* parent = nullptr);

    QString name() const;
    QLineEdit* nameEdit() const { return name_; }
    QPushButton* useButton() const { return use_; }

private:
    QLineEdit* name_ = nullptr;
    QPushButton* use_ = nullptr;
};

}  // namespace bld::ui
