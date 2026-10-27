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

}  // namespace bld::ui
