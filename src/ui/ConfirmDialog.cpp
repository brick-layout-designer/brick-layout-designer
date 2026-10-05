#include "ConfirmDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::ui {

ConfirmDialog::ConfirmDialog(const ConfirmOptions& opts, QWidget* parent) : QDialog(parent), opts_(opts) {
    setObjectName(QStringLiteral("ConfirmDialog"));
    setWindowTitle(opts.title);
    setModal(true);
    auto* col = new QVBoxLayout(this);
    col->setSpacing(10);
    auto* title = new QLabel(opts.title, this);
    title->setObjectName(QStringLiteral("confirmTitle"));
    title->setWordWrap(true);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setBold(true);
    title->setFont(f);
    col->addWidget(title);

    QStringList lines;
    for (const QString& s : { opts.removes, opts.keeps })
        if (!s.isEmpty()) lines << s.toHtmlEscaped();
    if (!opts.undo.isEmpty()) lines << QStringLiteral("<b>%1</b>").arg(opts.undo.toHtmlEscaped());
    body_ = new QLabel(lines.join(QStringLiteral("<br><br>")), this);
    body_->setObjectName(QStringLiteral("confirmBody"));
    body_->setTextFormat(Qt::RichText);
    body_->setWordWrap(true);
    col->addWidget(body_);

    if (!opts.typeName.isEmpty()) {
        auto* hint = new QLabel(tr("Type <b>%1</b> to confirm").arg(opts.typeName.toHtmlEscaped()), this);
        hint->setTextFormat(Qt::RichText);
        col->addWidget(hint);
        typed_ = new QLineEdit(this);
        typed_->setObjectName(QStringLiteral("confirmName"));
        typed_->setAccessibleName(tr("Type %1 to confirm").arg(opts.typeName));
        hint->setBuddy(typed_);
        col->addWidget(typed_);
        connect(typed_, &QLineEdit::textChanged, this, &ConfirmDialog::update);
        connect(typed_, &QLineEdit::returnPressed, this, [this] {
            if (confirm_->isEnabled()) accept();
        });
    }

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setObjectName(QStringLiteral("confirmCancel"));
    confirm_ = new QPushButton(opts.confirmLabel.isEmpty() ? tr("Delete") : opts.confirmLabel, this);
    confirm_->setObjectName(QStringLiteral("confirmOk"));
    confirm_->setProperty(opts.danger ? "danger" : "accent", true);
    // Enter never deletes by itself: neither button is the default.
    for (QPushButton* b : { cancel_, confirm_ }) {
        b->setAutoDefault(false);
        b->setDefault(false);
        b->setMinimumHeight(36); // easy to hit with a finger
    }
    row->addWidget(cancel_);
    row->addWidget(confirm_);
    col->addLayout(row);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirm_, &QPushButton::clicked, this, [this] {
        if (confirm_->isEnabled()) accept();
    });
    setMinimumWidth(380);
    update();
    cancel_->setFocus(Qt::OtherFocusReason);
}

void ConfirmDialog::update() {
    confirm_->setEnabled(!typed_ || typedMatches(typed_->text(), opts_.typeName));
}

QString ConfirmDialog::bodyText() const { return body_->text(); }

bool ConfirmDialog::typedMatches(const QString& typed, const QString& name) {
    return typed.trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0;
}

bool ConfirmDialog::ask(QWidget* parent, const ConfirmOptions& opts) {
    ConfirmDialog d(opts, parent);
    return d.exec() == QDialog::Accepted;
}

ConfirmOptions ConfirmDialog::deleteOptions(const QString& name, const DeleteWording& w) {
    const QString verb = w.verb.isEmpty() ? tr("Delete") : w.verb;
    ConfirmOptions o;
    o.title = w.title.isEmpty() ? tr("%1 “%2”?").arg(verb, name) : w.title;
    o.removes = w.removes.isEmpty() ? tr("“%1” is deleted.").arg(name) : w.removes;
    o.keeps = w.keeps;
    o.undo = w.undo.isEmpty() ? tr("This can’t be undone.") : w.undo;
    o.confirmLabel = verb;
    if (w.typeName) o.typeName = name;
    return o;
}

bool ConfirmDialog::confirmDelete(QWidget* parent, const QString& name, const DeleteWording& w) {
    return ask(parent, deleteOptions(name, w));
}

QString ConfirmDialog::undoWithCtrlZ() { return tr("You can undo this with Ctrl+Z."); }

DeleteWording ConfirmDialog::layoutWording() {
    DeleteWording w;
    w.removes = tr("The layout, its history and its share links are deleted for everyone who can open it.");
    w.keeps = tr("The modules, parts and venues it uses aren’t deleted.");
    w.typeName = true;
    return w;
}

DeleteWording ConfirmDialog::moduleWording() {
    DeleteWording w;
    w.removes = tr("The module and its versions are deleted, and it leaves any collections it was in.");
    w.keeps = tr("Layouts that already use it don’t change.");
    return w;
}

DeleteWording ConfirmDialog::venueWording() {
    DeleteWording w;
    w.removes = tr("The venue is deleted from the Venue library.");
    w.keeps = tr("Layouts made from it keep their own copy of the venue.");
    return w;
}

DeleteWording ConfirmDialog::customPartWording() {
    DeleteWording w;
    w.removes = tr("The part is deleted from your custom parts.");
    w.keeps = tr("Layouts that use it show a placeholder in its place.");
    return w;
}

} // namespace bld::ui
