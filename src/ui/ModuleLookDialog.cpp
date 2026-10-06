#include "ModuleLookDialog.h"

#include "help/HelpButton.h"

#include "../core/ModuleLook.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace bld::ui {

namespace {
QIcon swatch(const QString& hex) {
    QPixmap px(28, 16);
    px.fill(QColor(hex));
    return QIcon(px);
}
}  // namespace

ModuleLookDialog::ModuleLookDialog(Current current, Apply apply, QWidget* parent)
    : QDialog(parent), current_(std::move(current)), apply_(std::move(apply)) {
    setWindowTitle(tr("Module look"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    title_ = new QLabel(this);
    QFont tf = title_->font();
    tf.setBold(true);
    title_->setFont(tf);
    head->addWidget(title_, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("dialog.moduleLook"), this));
    col->addLayout(head);

    showName_ = new QCheckBox(tr("Show name"), this);
    col->addWidget(showName_);

    auto* form = new QFormLayout();
    outline_ = new QPushButton(this);
    outline_->setAccessibleName(tr("Outline colour"));
    name_ = new QPushButton(this);
    name_->setAccessibleName(tr("Name colour"));
    form->addRow(tr("Outline colour"), outline_);
    form->addRow(tr("Name colour"), name_);
    col->addLayout(form);
    same_ = new QCheckBox(tr("Same colour"), this);
    same_->setToolTip(tr("The outline and the name change together"));
    col->addWidget(same_);

    auto* buttons = new QDialogButtonBox(this);
    reset_ = buttons->addButton(tr("Reset to default"), QDialogButtonBox::ResetRole);
    auto* done = buttons->addButton(tr("Done"), QDialogButtonBox::AcceptRole);
    done->setDefault(true);
    col->addWidget(buttons);

    connect(showName_, &QCheckBox::toggled, this, [this](bool on) {
        if (refreshing_) return;
        if (const auto* m = current_()) apply_(core::withShowName(*m, on), on ? tr("Show module name") : tr("Hide module name"));
        refresh();
    });
    connect(same_, &QCheckBox::toggled, this, [this](bool on) {
        if (refreshing_) return;
        if (const auto* m = current_()) apply_(core::withSameColour(*m, on), tr("Module colours"));
        refresh();
    });
    connect(outline_, &QPushButton::clicked, this, [this] { chooseColour(true); });
    connect(name_, &QPushButton::clicked, this, [this] { chooseColour(false); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (const auto* m = current_()) apply_(core::withDefaultColours(*m), tr("Reset module colours"));
        refresh();
    });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    refresh();
}

void ModuleLookDialog::refresh() {
    const core::Module* m = current_();
    if (!m) {
        // Deleted here or by someone else: nothing left to edit.
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }
    refreshing_ = true;
    title_->setText(m->name.isEmpty() ? tr("(module)") : m->name);
    showName_->setChecked(m->showName);
    same_->setChecked(core::coloursLinked(*m));
    const QString own = defaultColour ? defaultColour() : QString();
    const QString fallback = own.isEmpty() ? core::kModuleDefaultColour : own;
    const QString o = core::moduleColour(*m, core::ModuleColourPart::Outline, fallback);
    const QString n = core::moduleColour(*m, core::ModuleColourPart::Name, fallback);
    outline_->setIcon(swatch(o));
    outline_->setText(o);
    name_->setIcon(swatch(n));
    name_->setText(n);
    reset_->setEnabled(core::hasCustomColours(*m));
    refreshing_ = false;
}

void ModuleLookDialog::pickColour(bool outline, const QColor& c) {
    const auto* m = current_();
    if (!m || !c.isValid()) return;
    apply_(core::withColour(*m, outline ? core::ModuleColourPart::Outline : core::ModuleColourPart::Name, c.name()),
           tr("Module colours"));
    refresh();
}

void ModuleLookDialog::chooseColour(bool outline) {
    const auto* m = current_();
    if (!m) return;
    const QString own = defaultColour ? defaultColour() : QString();
    const QColor start(core::moduleColour(*m, outline ? core::ModuleColourPart::Outline : core::ModuleColourPart::Name,
                                          own.isEmpty() ? core::kModuleDefaultColour : own));
    const QColor c = QColorDialog::getColor(start, this, outline ? tr("Outline colour") : tr("Name colour"));
    pickColour(outline, c);
}

}  // namespace bld::ui
