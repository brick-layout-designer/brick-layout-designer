#include "SaveSetDialog.h"

#include "help/HelpButton.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::ui {

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
    connect(name_, &QLineEdit::textChanged, this, [this] { save_->setEnabled(!name_->text().trimmed().isEmpty()); });
    col->addWidget(buttons);
}

QString SaveSetDialog::name() const {
    const QString n = name_->text().trimmed();
    return n.isEmpty() ? tr("New Set") : n;
}

bool SaveSetDialog::alsoOnServer() const {
    return !server_->isHidden() && server_->isChecked();
}

}  // namespace bld::ui
