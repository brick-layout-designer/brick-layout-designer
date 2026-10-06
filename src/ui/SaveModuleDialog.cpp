#include "SaveModuleDialog.h"
#include "ConfirmDialog.h"
#include "ReturnWording.h"
#include "ServerLibrary.h"
#include "help/HelpButton.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace bld::ui {

SaveModuleDialog::SaveModuleDialog(ServerLibrary& library, const QString& defaultOwner, QWidget* parent)
    : QDialog(parent), library_(library) {
    setObjectName(QStringLiteral("saveModuleDialog"));
    setWindowTitle(tr("Save to Module library"));
    auto* col = new QVBoxLayout(this);

    const QList<sync::ServerModule> editable = library_.editableModules();
    auto* intro = new QLabel(tr("A copy goes to your Module library, so you and your club can use it in other layouts. "
                                "This module stays linked to it."),
                             this);
    intro->setWordWrap(true);
    col->addWidget(intro);
    new_ = new QRadioButton(tr("New module in the Module library"), this);
    new_->setObjectName(QStringLiteral("saveModuleNew"));
    update_ = new QRadioButton(editable.isEmpty() ? tr("New version of a module in the Module library (none you can change yet)")
                                                  : tr("New version of a module in the Module library"),
                               this);
    update_->setObjectName(QStringLiteral("saveModuleUpdate"));
    update_->setEnabled(!editable.isEmpty());
    new_->setChecked(true);
    auto* group = new QButtonGroup(this);
    group->addButton(new_);
    group->addButton(update_);
    col->addWidget(new_);
    col->addWidget(update_);

    newPage_ = new QWidget(this);
    auto* newPage = newPage_;
    auto* form = new QFormLayout(newPage);
    form->setContentsMargins(0, 0, 0, 0);
    name_ = new QLineEdit(newPage);
    name_->setObjectName(QStringLiteral("saveModuleName"));
    name_->setPlaceholderText(tr("My module"));
    form->addRow(tr("Module name"), name_);
    saveTo_ = new QComboBox(newPage);
    saveTo_->setObjectName(QStringLiteral("saveModuleOwner"));
    saveTo_->addItem(tr("Me (on %1)").arg(library_.label()), QString());
    for (const auto& o : library_.orgs()) saveTo_->addItem(o.name, o.slug);
    saveTo_->addItem(tr("This computer only (Module library folder)"), QString::fromLatin1(kThisComputer));
    const int def = saveTo_->findData(defaultOwner);
    saveTo_->setCurrentIndex(def >= 0 ? def : 0);
    form->addRow(tr("Save to"), saveTo_);
    col->addWidget(newPage);

    targets_ = new QListWidget(this);
    targets_->setMaximumHeight(240);
    targets_->setObjectName(QStringLiteral("saveModuleTargets"));
    targets_->setIconSize(QSize(40, 40));
    for (const auto& m : editable) {
        auto* it = new QListWidgetItem(m.latestVersion > 0 ? tr("%1  ·  v%2").arg(m.title).arg(m.latestVersion) : m.title,
                                       targets_);
        it->setData(Qt::UserRole, m.id);
        if (const QString path = m.thumbnailPath(); !path.isEmpty())
            library_.picture(path + QStringLiteral("&size=small"), targets_, [it](const QImage& img) {
                if (!img.isNull()) it->setIcon(QIcon(QPixmap::fromImage(img)));
            });
    }
    targets_->setAccessibleName(tr("Module to update"));
    col->addWidget(targets_);

    auto* noteForm = new QFormLayout();
    note_ = new QLineEdit(this);
    note_->setObjectName(QStringLiteral("saveModuleNote"));
    note_->setMaxLength(300);
    noteForm->addRow(tr("What changed? (optional)"), note_);
    col->addLayout(noteForm);

    // The module's sheets (setSheets): shown only with two or more.
    sheetsBox_ = new QWidget(this);
    sheetsBox_->setObjectName(QStringLiteral("saveModuleSheets"));
    auto* sheetsCol = new QVBoxLayout(sheetsBox_);
    sheetsCol->setContentsMargins(0, 0, 0, 0);
    auto* sheetsRow = new QHBoxLayout();
    sheetsLabel_ = new QLabel(sheetsBox_);
    sheetsLabel_->setObjectName(QStringLiteral("saveModuleSheetsText"));
    sheetsLabel_->setWordWrap(true);
    sheetsLabel_->setTextFormat(Qt::RichText);
    sheetsRow->addWidget(sheetsLabel_, 1);
    sheetsRow->addWidget(new help::HelpButton(QStringLiteral("module.sheets"), sheetsBox_));
    sheetsCol->addLayout(sheetsRow);
    oneSheet_ = new QCheckBox(tr("Put everything on one sheet"), sheetsBox_);
    oneSheet_->setObjectName(QStringLiteral("saveModuleOneSheet"));
    sheetsCol->addWidget(oneSheet_);
    sheetsBox_->setVisible(false);
    col->addWidget(sheetsBox_);
    connect(oneSheet_, &QCheckBox::toggled, this, &SaveModuleDialog::showSheets);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("saveModuleError"));
    error_->setProperty("danger", true);
    error_->setWordWrap(true);
    error_->setVisible(false);
    col->addWidget(error_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* save = buttons->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("saveModuleSave"));
    save->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addWidget(buttons);

    connect(new_, &QRadioButton::toggled, this, &SaveModuleDialog::showMode);
    connect(saveTo_, &QComboBox::currentIndexChanged, this, &SaveModuleDialog::showMode);
    showMode();
    name_->setFocus();
    confirmSaveToClub = [this](const QString& club) {
        return ConfirmDialog::ask(this, saveToClubOptions(club));
    };
}

void SaveModuleDialog::setModuleName(const QString& name) {
    setWindowTitle(tr("Save “%1” to the Module library").arg(name.isEmpty() ? tr("module") : name));
    name_->setText(name);
}

void SaveModuleDialog::showMode() {
    const bool isNew = new_->isChecked();
    newPage_->setVisible(isNew);
    targets_->setVisible(!isNew);
    adjustSize();
    // Versions and notes are the server's; a module on this computer is one file.
    const bool local = isNew && saveTo_->currentData().toString() == QLatin1String(kThisComputer);
    note_->setEnabled(!local);
    note_->setPlaceholderText(isNew ? tr("e.g. First version") : tr("e.g. Longer siding"));
    error_->setVisible(false);
}

void SaveModuleDialog::setSheets(const QStringList& names, const QString& oneSheetName) {
    sheets_ = names;
    oneSheetName_ = oneSheetName;
    sheetsBox_->setVisible(names.size() > 1);
    showSheets();
    adjustSize();
}

void SaveModuleDialog::showSheets() {
    QStringList shown;
    for (const QString& n : sheets_) shown << (n.isEmpty() ? tr("untitled") : n).toHtmlEscaped();
    sheetsLabel_->setText(oneSheet_->isChecked()
                              ? tr("All its parts go on one sheet: <b>%1</b>.").arg(oneSheetName_.toHtmlEscaped())
                              : tr("This module uses %1 sheets: <b>%2</b>.").arg(sheets_.size()).arg(shown.join(QStringLiteral(", "))));
}

SaveModuleDialog::Choice SaveModuleDialog::choice() const {
    Choice c;
    c.oneSheet = sheets_.size() > 1 && oneSheet_->isChecked();
    if (update_->isChecked()) {
        if (QListWidgetItem* it = targets_->currentItem()) c.updateId = it->data(Qt::UserRole).toString();
    } else {
        c.title = name_->text().trimmed();
        const QString owner = saveTo_->currentData().toString();
        c.onThisComputer = owner == QLatin1String(kThisComputer);
        if (!c.onThisComputer) c.orgSlug = owner;
    }
    c.note = c.onThisComputer ? QString() : note_->text().trimmed();
    return c;
}

void SaveModuleDialog::accept() {
    const Choice c = choice();
    QString problem;
    if (update_->isChecked() && c.updateId.isEmpty()) problem = tr("Pick the module to update.");
    else if (!update_->isChecked() && c.title.isEmpty()) problem = tr("Module name is required");
    if (!problem.isEmpty()) {
        error_->setText(problem);
        error_->setVisible(true);
        return;
    }
    if (c.updateId.isEmpty() && !c.onThisComputer && !c.orgSlug.isEmpty() && confirmSaveToClub
        && !confirmSaveToClub(saveTo_->currentText()))
        return;
    QDialog::accept();
}

}  // namespace bld::ui
