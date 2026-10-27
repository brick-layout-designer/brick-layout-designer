#include "SaveSetDialog.h"

#include "help/HelpButton.h"
#include "saveload/SetIO.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::ui {

namespace {

// The red line under a set's name, hidden until the name is one of its parts'.
QLabel* clashLine(const QString& objectName, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: #d9534f;")); // reads on light and dark themes
    label->hide();
    return label;
}

// Why `name` can't be a set of `subparts`, or empty when it can.
QString clashText(const QString& name, const QStringList& subparts) {
    const QString part = saveload::partNamedLikeSet(name, subparts);
    if (part.isEmpty()) return {};
    return QObject::tr("“%1” is a part in this set, so the set can’t have that name too. Pick another name.")
        .arg(part);
}

} // namespace

SaveSetDialog::SaveSetDialog(int partCount, const QString& serverLabel, bool alsoOnServer, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("saveSetDialog"));
    setWindowTitle(tr("Save Selection as Set"));
    auto* col = new QVBoxLayout(this);

    auto* about = new QHBoxLayout();
    auto* intro = new QLabel(tr("A set is a group of parts saved together, to place again in one go. "
                                "It goes into your parts, in the Parts panel."),
                             this);
    intro->setObjectName(QStringLiteral("saveSetAbout"));
    intro->setWordWrap(true);
    about->addWidget(intro, 1);
    about->addWidget(new help::HelpButton(QStringLiteral("set.about"), this, intro), 0, Qt::AlignTop);
    col->addLayout(about);

    auto* count = new QLabel(tr("%n part(s) picked.", nullptr, partCount), this);
    count->setObjectName(QStringLiteral("saveSetCount"));
    col->addWidget(count);

    auto* form = new QFormLayout();
    name_ = new QLineEdit(tr("New Set"), this);
    name_->setObjectName(QStringLiteral("saveSetName"));
    name_->setPlaceholderText(tr("New Set"));
    name_->selectAll();
    form->addRow(tr("Set name"), name_);
    col->addLayout(form);
    clash_ = clashLine(QStringLiteral("saveSetNameClash"), this);
    col->addWidget(clash_);

    server_ = new QCheckBox(tr("Also put it on the server"), this);
    server_->setObjectName(QStringLiteral("saveSetAlsoOnServer"));
    server_->setToolTip(tr("Adds it to your custom parts on %1 too, so you can place it on the website and share it "
                           "like one of your own parts.")
                            .arg(serverLabel));
    server_->setChecked(alsoOnServer);
    server_->setVisible(!serverLabel.isEmpty());
    col->addWidget(server_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    save_ = buttons->button(QDialogButtonBox::Save);
    save_->setText(tr("Save set"));
    save_->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, &SaveSetDialog::check);
    col->addWidget(buttons);
}

void SaveSetDialog::refuseNamesOf(const QStringList& subpartKeys) {
    subparts_ = subpartKeys;
    check();
}

void SaveSetDialog::check() {
    const QString why = clashText(name(), subparts_);
    clash_->setText(why);
    clash_->setVisible(!why.isEmpty());
    save_->setEnabled(!name_->text().trimmed().isEmpty() && why.isEmpty());
}

QString SaveSetDialog::name() const {
    const QString n = name_->text().trimmed();
    return n.isEmpty() ? tr("New Set") : n;
}

bool SaveSetDialog::alsoOnServer() const {
    return !server_->isHidden() && server_->isChecked();
}

RenameSetDialog::RenameSetDialog(const QString& current, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("renameSetDialog"));
    setWindowTitle(tr("Rename set"));
    auto* col = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    name_ = new QLineEdit(current, this);
    name_->setObjectName(QStringLiteral("renameSetName"));
    name_->selectAll();
    form->addRow(tr("Set name"), name_);
    col->addLayout(form);
    auto* keeps = new QLabel(tr("Layouts that use it don’t change."), this);
    keeps->setWordWrap(true);
    col->addWidget(keeps);
    server_ = new QCheckBox(this);
    server_->setObjectName(QStringLiteral("renameSetAlsoOnServer"));
    server_->setChecked(true);
    server_->hide();
    col->addWidget(server_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    rename_ = buttons->button(QDialogButtonBox::Ok);
    rename_->setText(tr("Rename"));
    rename_->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this,
            [this] { rename_->setEnabled(!name_->text().trimmed().isEmpty()); });
    col->addWidget(buttons);
}

void RenameSetDialog::showServerOption(const QString& serverLabel) {
    server_->setText(tr("Also rename it on %1").arg(serverLabel));
    server_->setToolTip(tr("Your set is in your custom parts on %1 too. Its name there changes as well; "
                           "layouts there don’t change.")
                            .arg(serverLabel));
    server_->show();
}

QString RenameSetDialog::name() const {
    return name_->text().trimmed();
}

bool RenameSetDialog::alsoOnServer() const {
    return !server_->isHidden() && server_->isChecked();
}

SetNameTakenDialog::SetNameTakenDialog(const QString& taken, const QString& suggested, const QString& serverLabel,
                                       QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("setNameTakenDialog"));
    setWindowTitle(tr("Name already used"));
    auto* col = new QVBoxLayout(this);
    auto* why = new QLabel(tr("You already have a part or set called “%1” on %2, so this set can’t go there "
                              "under that name. Give it another name, here and there:")
                               .arg(taken, serverLabel),
                           this);
    why->setObjectName(QStringLiteral("setNameTakenWhy"));
    why->setWordWrap(true);
    col->addWidget(why);
    auto* form = new QFormLayout();
    name_ = new QLineEdit(suggested, this);
    name_->setObjectName(QStringLiteral("setNameTakenName"));
    name_->selectAll();
    form->addRow(tr("Set name"), name_);
    col->addLayout(form);
    clash_ = clashLine(QStringLiteral("setNameTakenClash"), this);
    col->addWidget(clash_);
    auto* buttons = new QDialogButtonBox(this);
    use_ = buttons->addButton(tr("Use this name"), QDialogButtonBox::AcceptRole);
    use_->setDefault(true);
    buttons->addButton(tr("Keep it only on this computer"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, &SetNameTakenDialog::check);
    col->addWidget(buttons);
}

QString SetNameTakenDialog::name() const {
    return name_->text().trimmed();
}

void SetNameTakenDialog::refuseNamesOf(const QStringList& subpartKeys) {
    subparts_ = subpartKeys;
    check();
}

void SetNameTakenDialog::check() {
    const QString why = name().isEmpty() ? QString() : clashText(name(), subparts_);
    clash_->setText(why);
    clash_->setVisible(!why.isEmpty());
    use_->setEnabled(!name().isEmpty() && why.isEmpty());
}

}  // namespace bld::ui
