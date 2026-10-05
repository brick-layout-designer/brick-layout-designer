#pragma once

// One confirmation dialog for every deletion (and the other "are you
// sure?" questions), worded like the website's (apps/web/src/ui/
// ConfirmDialog.tsx): "Delete ‹name›?", what goes, what stays, whether it
// can be undone, a red Delete and a Cancel that starts with the focus.
// Esc cancels. For big or permanent deletions the person types the name
// before Delete turns on.

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

namespace bld::ui {

struct ConfirmOptions {
    QString title;         // "Delete “Main yard”?"
    QString removes;       // what will be removed
    QString keeps;         // what stays
    QString undo;          // whether it can be undone
    QString confirmLabel;  // the red button; empty = "Delete"
    QString typeName;      // when set, typed before the button turns on
    bool danger = true;    // red; false = the accent colour (not a removal)
};

struct DeleteWording {
    QString removes;       // empty = "“‹name›” is deleted."
    QString keeps;
    QString undo;          // empty = "This can’t be undone."
    bool typeName = false; // type the name first
    QString verb;          // empty = "Delete" ("Remove", "Revoke"…)
    QString title;         // a whole title instead of "‹verb› “‹name›”?"
};

class ConfirmDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfirmDialog(const ConfirmOptions& opts, QWidget* parent = nullptr);

    /** Ask and wait: true only for the confirm button. */
    static bool ask(QWidget* parent, const ConfirmOptions& opts);
    /** The standard deletion question. */
    static bool confirmDelete(QWidget* parent, const QString& name, const DeleteWording& w = {});
    static ConfirmOptions deleteOptions(const QString& name, const DeleteWording& w = {});
    /** Same name, ignoring case and the spaces around it. */
    static bool typedMatches(const QString& typed, const QString& name);

    // The same words as the website's ui/deleteWording.ts.
    static DeleteWording layoutWording();
    static DeleteWording moduleWording();
    static DeleteWording venueWording();
    static DeleteWording customPartWording();

    static QString undoWithCtrlZ();

    QPushButton* confirmButton() const { return confirm_; }
    QPushButton* cancelButton() const { return cancel_; }
    QLineEdit* nameEdit() const { return typed_; }
    QString bodyText() const;

private:
    void update();

    ConfirmOptions opts_;
    QLabel* body_ = nullptr;
    QLineEdit* typed_ = nullptr;
    QPushButton* confirm_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

} // namespace bld::ui
