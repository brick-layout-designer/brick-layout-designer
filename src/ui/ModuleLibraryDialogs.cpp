#include "ModuleLibraryDialogs.h"
#include "ConfirmDialog.h"
#include "ReturnWording.h"
#include "ServerLibrary.h"
#include "help/HelpButton.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::ui {

class ModuleSheetsBox : public QWidget {
public:
    explicit ModuleSheetsBox(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("moduleSheets"));
        auto* col = new QVBoxLayout(this);
        col->setContentsMargins(0, 0, 0, 0);
        auto* row = new QHBoxLayout();
        label_ = new QLabel(this);
        label_->setObjectName(QStringLiteral("moduleSheetsText"));
        label_->setWordWrap(true);
        label_->setTextFormat(Qt::RichText);
        row->addWidget(label_, 1);
        row->addWidget(new help::HelpButton(QStringLiteral("module.sheets"), this));
        col->addLayout(row);
        one_ = new QCheckBox(QCoreApplication::translate("bld::ui::SaveModuleDialog", "Put everything on one sheet"), this);
        one_->setObjectName(QStringLiteral("moduleOneSheet"));
        col->addWidget(one_);
        QObject::connect(one_, &QCheckBox::toggled, this, [this] { refresh(); });
        setVisible(false);
    }
    void set(const QStringList& names, const QString& oneName) {
        names_ = names;
        oneName_ = oneName;
        refresh();
    }
    bool many() const { return names_.size() > 1; }
    bool oneSheet() const { return many() && one_->isChecked(); }
    QCheckBox* box() const { return one_; }

private:
    void refresh() {
        setVisible(many());
        QStringList shown;
        for (const QString& n : names_)
            shown << (n.isEmpty() ? QCoreApplication::translate("bld::ui::SaveModuleDialog", "untitled") : n).toHtmlEscaped();
        label_->setText(one_->isChecked()
                            ? QCoreApplication::translate("bld::ui::SaveModuleDialog", "All its parts go on one sheet: <b>%1</b>.")
                                  .arg(oneName_.toHtmlEscaped())
                            : QCoreApplication::translate("bld::ui::SaveModuleDialog", "This module uses %1 sheets: <b>%2</b>.")
                                  .arg(names_.size())
                                  .arg(shown.join(QStringLiteral(", "))));
    }
    QLabel* label_ = nullptr;
    QCheckBox* one_ = nullptr;
    QStringList names_;
    QString oneName_;
};

namespace {
constexpr const char* kThisComputer = "\x01local";
}

MakeModuleDialog::MakeModuleDialog(ServerLibrary* library, const QString& defaultOwner, int partCount, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("makeModuleDialog"));
    setWindowTitle(tr("Make a module"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    auto* intro = new QLabel(partCount == 1 ? tr("The picked part becomes one module in this layout. It moves as one piece.")
                                            : tr("The %1 picked parts become one module in this layout. It moves as one piece.")
                                                  .arg(partCount),
                             this);
    intro->setWordWrap(true);
    head->addWidget(intro, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("module.make"), this));
    col->addLayout(head);

    auto* form = new QFormLayout();
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("makeModuleName"));
    name_->setPlaceholderText(tr("e.g. Desert corner"));
    form->addRow(tr("Module name"), name_);
    col->addLayout(form);

    alsoSave_ = new QCheckBox(tr("Also save to my Module library, to use it in other layouts"), this);
    alsoSave_->setObjectName(QStringLiteral("makeModuleAlsoSave"));
    col->addWidget(alsoSave_);

    saveBox_ = new QWidget(this);
    auto* saveCol = new QVBoxLayout(saveBox_);
    saveCol->setContentsMargins(0, 0, 0, 0);
    auto* saveForm = new QFormLayout();
    saveTo_ = new QComboBox(saveBox_);
    saveTo_->setObjectName(QStringLiteral("makeModuleOwner"));
    if (library && library->state() == ServerLibrary::State::Ready) {
        saveTo_->addItem(tr("Me (on %1)").arg(library->label()), QString());
        for (const auto& o : library->orgs())
            if (o.canAdd) saveTo_->addItem(o.name, o.slug);
    }
    saveTo_->addItem(tr("This computer only (Module library folder)"), QString::fromLatin1(kThisComputer));
    const int def = saveTo_->findData(defaultOwner);
    saveTo_->setCurrentIndex(def >= 0 ? def : 0);
    saveForm->addRow(tr("Save to"), saveTo_);
    saveCol->addLayout(saveForm);
    sheets_ = new ModuleSheetsBox(saveBox_);
    saveCol->addWidget(sheets_);
    col->addWidget(saveBox_);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("makeModuleError"));
    error_->setProperty("danger", true);
    error_->setWordWrap(true);
    error_->setVisible(false);
    col->addWidget(error_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    make_ = buttons->addButton(tr("Make module"), QDialogButtonBox::AcceptRole);
    make_->setObjectName(QStringLiteral("makeModuleMake"));
    make_->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addWidget(buttons);

    connect(alsoSave_, &QCheckBox::toggled, this, &MakeModuleDialog::showSave);
    showSave();
    name_->setFocus();
    confirmSaveToClub = [this](const QString& club) { return ConfirmDialog::ask(this, saveToClubOptions(club)); };
}

void MakeModuleDialog::showSave() {
    saveBox_->setVisible(alsoSave_->isChecked());
    make_->setText(alsoSave_->isChecked() ? tr("Make and save") : tr("Make module"));
    adjustSize();
}

void MakeModuleDialog::setSheets(const QStringList& names, const QString& oneSheetName) {
    sheets_->set(names, oneSheetName);
    adjustSize();
}

QCheckBox* MakeModuleDialog::oneSheetBox() const { return sheets_->box(); }

MakeModuleDialog::Choice MakeModuleDialog::choice() const {
    Choice c;
    c.name = name_->text().trimmed();
    c.alsoSave = alsoSave_->isChecked();
    if (c.alsoSave) {
        const QString owner = saveTo_->currentData().toString();
        c.onThisComputer = owner == QLatin1String(kThisComputer);
        if (!c.onThisComputer) c.orgSlug = owner;
        c.oneSheet = sheets_->oneSheet();
    }
    return c;
}

void MakeModuleDialog::accept() {
    const Choice c = choice();
    if (c.name.isEmpty()) {
        error_->setText(tr("Give the module a name."));
        error_->setVisible(true);
        return;
    }
    if (c.alsoSave && !c.orgSlug.isEmpty() && confirmSaveToClub && !confirmSaveToClub(saveTo_->currentText())) return;
    QDialog::accept();
}

PublishModuleDialog::PublishModuleDialog(const QString& libraryTitle, int version, int latest, bool canPublish, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("publishModuleDialog"));
    setWindowTitle(tr("Update Module library version"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    text_ = new QLabel(tr("This module’s parts in this layout become version %1 of <b>%2</b> in the Module library. "
                          "Layouts that use it can then update to it.")
                           .arg(latest + 1)
                           .arg(libraryTitle.toHtmlEscaped()),
                       this);
    text_->setObjectName(QStringLiteral("publishModuleText"));
    text_->setTextFormat(Qt::RichText);
    text_->setWordWrap(true);
    head->addWidget(text_, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("module.library"), this));
    col->addLayout(head);
    newer_ = new QLabel(tr("The Module library has version %1, newer than the version %2 this layout has. Saving "
                           "puts your copy on top; what changed in version %1 isn’t in it. To keep those changes, "
                           "use Update from Module library first.")
                            .arg(latest)
                            .arg(version),
                        this);
    newer_->setObjectName(QStringLiteral("publishModuleNewer"));
    newer_->setWordWrap(true);
    newer_->setVisible(version > 0 && latest > version);
    col->addWidget(newer_);
    if (!canPublish) {
        auto* cant = new QLabel(tr("You can’t change “%1” in the Module library. Use Save to Module library… to save your own copy "
                                   "instead.")
                                    .arg(libraryTitle),
                                this);
        cant->setProperty("danger", true);
        cant->setWordWrap(true);
        col->addWidget(cant);
    }
    auto* form = new QFormLayout();
    note_ = new QLineEdit(this);
    note_->setObjectName(QStringLiteral("publishModuleNote"));
    note_->setMaxLength(300);
    note_->setPlaceholderText(tr("e.g. Longer siding"));
    form->addRow(tr("What changed? (optional)"), note_);
    col->addLayout(form);
    sheets_ = new ModuleSheetsBox(this);
    col->addWidget(sheets_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    save_ = buttons->addButton(tr("Save version"), QDialogButtonBox::AcceptRole);
    save_->setObjectName(QStringLiteral("publishModuleSave"));
    save_->setDefault(true);
    save_->setEnabled(canPublish);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addWidget(buttons);
    note_->setFocus();
}

void PublishModuleDialog::setSheets(const QStringList& names, const QString& oneSheetName) {
    sheets_->set(names, oneSheetName);
    adjustSize();
}

QString PublishModuleDialog::note() const { return note_->text().trimmed(); }
bool PublishModuleDialog::oneSheet() const { return sheets_->oneSheet(); }

}  // namespace bld::ui
