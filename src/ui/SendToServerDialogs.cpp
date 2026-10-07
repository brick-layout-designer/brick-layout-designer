#include "SendToServerDialogs.h"

#include "ConfirmDialog.h"
#include "ReturnWording.h"
#include "ServerLibrary.h"
#include "help/HelpButton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace bld::ui {

// ---------- SaveToServerDialog ------------------------------------------------

SaveToServerDialog::SaveToServerDialog(const QString& what, const QString& initialName, const QString& server,
                                       const QList<sync::OrgEntry>& orgs, const QString& initialOwner, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("SaveToServerDialog"));
    setWindowTitle(tr("Save to Server"));
    auto* col = new QVBoxLayout(this);
    auto* intro = libraryMutedLabel(tr("A copy of this %1 goes to %2, where you, your clubs and the web can use it. "
                                       "The one on this computer stays as it is.")
                                        .arg(what, server),
                                    this);
    col->addWidget(help::withHelp(intro, QStringLiteral("server.send"), this));
    auto* form = new QFormLayout();
    name_ = new QLineEdit(initialName, this);
    name_->setObjectName(QStringLiteral("saveName"));
    name_->setMaxLength(80);
    owner_ = new QComboBox(this);
    owner_->setObjectName(QStringLiteral("saveOwner"));
    owner_->addItem(tr("Me"), QString());
    // Only the clubs that will take it (one that keeps adding to its admins is left out).
    for (const auto& o : orgs)
        if (o.canAdd) owner_->addItem(o.name, o.slug);
    const int start = initialOwner.isEmpty() ? -1 : owner_->findData(initialOwner);
    owner_->setCurrentIndex(start > 0 ? start : 0);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Save to"), help::withHelp(owner_, QStringLiteral("publish.owner"), this));
    col->addLayout(form);
    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("saveError"));
    error_->setProperty("danger", true);
    error_->setWordWrap(true);
    error_->hide();
    col->addWidget(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* go = buttons->addButton(tr("Save to server"), QDialogButtonBox::AcceptRole);
    go->setObjectName(QStringLiteral("saveToServer"));
    go->setProperty("accent", true);
    go->setDefault(true);
    col->addWidget(buttons);
    confirmClub = [this](const QString& club) { return ConfirmDialog::ask(this, saveToClubOptions(club)); };
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, &SaveToServerDialog::save);
}

QString SaveToServerDialog::name() const { return name_->text().trimmed(); }
QString SaveToServerDialog::orgSlug() const { return owner_->currentData().toString(); }

void SaveToServerDialog::save() {
    if (name().isEmpty()) {
        error_->setText(tr("Give it a name."));
        error_->show();
        return;
    }
    // Into a club: say what that means first (the club owns it, you stay its author).
    if (!orgSlug().isEmpty() && confirmClub && !confirmClub(owner_->currentText())) return;
    accept();
}

// ---------- ShareToCatalogDialog ----------------------------------------------

ShareToCatalogDialog::ShareToCatalogDialog(sync::LibraryApi& api, const sync::CatalogShare& what, bool isUpdate, bool review,
                                           const QString& clubReview, QWidget* parent)
    : QDialog(parent), api_(api), base_(what), update_(isUpdate), clubReview_(clubReview) {
    setObjectName(QStringLiteral("ShareToCatalogDialog"));
    setWindowTitle(isUpdate ? tr("Publish this update") : tr("Share to the public catalog"));
    setMinimumWidth(460);
    openUrl = [](const QUrl& u) { QDesktopServices::openUrl(u); };
    auto* col = new QVBoxLayout(this);
    pages_ = new QStackedWidget(this);
    col->addWidget(pages_);

    // The form.
    auto* formPage = new QWidget(pages_);
    auto* f = new QVBoxLayout(formPage);
    f->setContentsMargins(0, 0, 0, 0);
    QString about = tr("A copy of it as it is now goes to the catalog. Later changes stay yours until you publish an update.");
    if (what.kind == QLatin1String("layout"))
        about += QLatin1Char(' ') + tr("Collaborators, comments, chat, file paths, the background picture and the venue’s notes stay private.");
    if (what.kind == QLatin1String("venue")) about += QLatin1Char(' ') + tr("Notes on the plan stay private.");
    if (review) about += QLatin1Char(' ') + tr("A moderator reviews it first.");
    auto* intro = libraryMutedLabel(about, formPage);
    intro->setObjectName(QStringLiteral("shareAbout"));
    f->addWidget(help::withHelp(intro, QStringLiteral("catalog.share"), formPage));
    auto* form = new QFormLayout();
    name_ = new QLineEdit(what.title, formPage);
    name_->setObjectName(QStringLiteral("shareName"));
    name_->setMaxLength(80);
    description_ = new QPlainTextEdit(formPage);
    description_->setObjectName(QStringLiteral("shareDescription"));
    description_->setFixedHeight(72);
    tags_ = new QLineEdit(formPage);
    tags_->setObjectName(QStringLiteral("shareTags"));
    tags_->setPlaceholderText(tr("e.g. station, 9V"));
    note_ = new QLineEdit(formPage);
    note_->setObjectName(QStringLiteral("shareNote"));
    note_->setMaxLength(300);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Description (optional)"), description_);
    form->addRow(tr("Tags, separated by commas (optional)"), tags_);
    if (isUpdate) form->addRow(tr("What changed? (optional)"), note_);
    else note_->hide();
    f->addLayout(form);
    consent_ = new QCheckBox(tr("Anyone can copy this into their own layouts."), formPage);
    consent_->setObjectName(QStringLiteral("shareConsent"));
    f->addWidget(consent_);
    error_ = new QLabel(formPage);
    error_->setObjectName(QStringLiteral("shareError"));
    error_->setProperty("danger", true);
    error_->setWordWrap(true);
    error_->hide();
    f->addWidget(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, formPage);
    shareBtn_ = buttons->addButton(isUpdate ? tr("Publish update") : tr("Share"), QDialogButtonBox::AcceptRole);
    shareBtn_->setObjectName(QStringLiteral("share"));
    shareBtn_->setProperty("accent", true);
    f->addWidget(buttons);
    pages_->addWidget(formPage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(shareBtn_, &QPushButton::clicked, this, &ShareToCatalogDialog::share);

    // What happened.
    auto* donePage = new QWidget(pages_);
    auto* d = new QVBoxLayout(donePage);
    d->setContentsMargins(0, 0, 0, 0);
    result_ = new QLabel(donePage);
    result_->setObjectName(QStringLiteral("shareResult"));
    result_->setWordWrap(true);
    d->addWidget(result_);
    d->addWidget(libraryMutedLabel(
        tr("Cards show its drawn picture. You can upload your own instead, like a photo of the real thing."), donePage));
    auto* doneRow = new QHBoxLayout();
    auto* cover = new QPushButton(tr("Choose a cover picture on the web…"), donePage);
    cover->setObjectName(QStringLiteral("shareCover"));
    auto* done = new QPushButton(tr("Done"), donePage);
    done->setObjectName(QStringLiteral("shareDone"));
    done->setProperty("accent", true);
    doneRow->addStretch(1);
    doneRow->addWidget(cover);
    doneRow->addWidget(done);
    d->addLayout(doneRow);
    pages_->addWidget(donePage);
    connect(cover, &QPushButton::clicked, this, [this] {
        if (openUrl && !itemId_.isEmpty()) openUrl(api_.catalogItemWebUrl(itemId_));
    });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
}

void ShareToCatalogDialog::share() {
    error_->hide();
    if (name_->text().trimmed().isEmpty()) {
        error_->setText(tr("Give it a name."));
        error_->show();
        return;
    }
    if (!consent_->isChecked()) {
        error_->setText(tr("Tick the box to agree that anyone can copy it."));
        error_->show();
        return;
    }
    shareBtn_->setEnabled(false);
    // A picture that can't be drawn doesn't stop the share: cards show a placeholder.
    if (makeThumbnail) makeThumbnail([this](const QByteArray& png) { send(png); });
    else send({});
}

void ShareToCatalogDialog::send(const QByteArray& png) {
    sync::CatalogShare s = base_;
    s.title = name_->text().trimmed();
    s.description = description_->toPlainText().trimmed();
    s.tags.clear();
    for (const QString& t : tags_->text().split(QLatin1Char(','))) {
        const QString tag = t.trimmed();
        if (!tag.isEmpty()) s.tags << tag;
    }
    s.note = note_->text().trimmed();
    s.thumbnailPng = png;
    s.update = update_;
    api_.shareToCatalog(s, [this](const QString& id, const QString& status) {
        itemId_ = id;
        status_ = status;
        result_->setText(status == QLatin1String("public") ? tr("Shared: it’s in the public catalog now.")
                         : !clubReview_.isEmpty()
                             ? tr("Sent to %1’s own review: its admins and managers check it before it appears in the catalog.")
                                   .arg(clubReview_)
                             : tr("Sent for review. A moderator looks at it before it appears in the catalog."));
        pages_->setCurrentIndex(1);
        emit shared(id, status);
    }, [this](const sync::ServerRefusal& r) {
        shareBtn_->setEnabled(true);
        error_->setText(ServerLibrary::refusalText(r));
        error_->show();
    });
}

}  // namespace bld::ui
