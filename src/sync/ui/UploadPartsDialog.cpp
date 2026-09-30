#include "UploadPartsDialog.h"

#include "ServerApi.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::sync {

UploadPartsDialog::UploadPartsDialog(ServerApi& api, PartsUpload& upload, const QList<LocalPart>& missing,
                                     QWidget* parent)
    : QDialog(parent), api_(api), upload_(upload), missing_(missing) {
    setWindowTitle(tr("Upload Parts to Server"));
    resize(520, 440);
    auto* col = new QVBoxLayout(this);
    col->addWidget(new QLabel(
        tr("These parts of yours aren't on the server yet. Upload the ones layouts there will need:"), this));
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("parts"));
    for (const auto& p : missing_) {
        auto* item = new QListWidgetItem(
            p.displayName == p.key ? p.key : QStringLiteral("%1 — %2").arg(p.key, p.displayName), list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    col->addWidget(list_, 1);
    auto* form = new QFormLayout();
    owner_ = new QComboBox(this);
    owner_->setObjectName(QStringLiteral("owner"));
    owner_->addItem(tr("You (personal)"), QString());
    form->addRow(tr("Owner"), owner_);
    col->addLayout(form);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("message"));
    message_->setWordWrap(true);
    col->addWidget(message_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    uploadBtn_ = buttons->addButton(tr("Upload"), QDialogButtonBox::AcceptRole);
    uploadBtn_->setObjectName(QStringLiteral("upload"));
    col->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(uploadBtn_, &QPushButton::clicked, this, &UploadPartsDialog::uploadChecked);

    connect(&api_, &ServerApi::orgsReady, this, [this](const QList<OrgEntry>& orgs) {
        for (const auto& o : orgs) owner_->addItem(o.name, o.slug);
    });
    connect(&upload_, &PartsUpload::uploaded, this, [this](int count, const QStringList& failed) {
        uploaded_ = count;
        if (failed.isEmpty()) {
            accept();
            return;
        }
        uploadBtn_->setEnabled(true);
        message_->setText(
            tr("Uploaded %1; these failed:\n%2").arg(count).arg(failed.join(QLatin1Char('\n'))));
    });
    api_.fetchOrgs();
}

void UploadPartsDialog::uploadChecked() {
    QList<LocalPart> chosen;
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->checkState() == Qt::Checked) chosen << missing_[i];
    if (chosen.isEmpty()) {
        reject();
        return;
    }
    uploadBtn_->setEnabled(false);
    message_->setText(tr("Uploading %n part(s)…", nullptr, static_cast<int>(chosen.size())));
    upload_.upload(chosen, owner_->currentData().toString());
}

} // namespace bld::sync
