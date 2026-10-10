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
    outline_->setAccessibleName(tr("Outline color"));
    name_ = new QPushButton(this);
    name_->setAccessibleName(tr("Name color"));
    form->addRow(tr("Outline color"), outline_);
    form->addRow(tr("Name color"), name_);
    col->addLayout(form);
    same_ = new QCheckBox(tr("Same color"), this);
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
        if (const auto* m = current_()) apply_(core::withSameColor(*m, on), tr("Module colors"));
        refresh();
    });
    connect(outline_, &QPushButton::clicked, this, [this] { chooseColor(true); });
    connect(name_, &QPushButton::clicked, this, [this] { chooseColor(false); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (const auto* m = current_()) apply_(core::withDefaultColors(*m), tr("Reset module colors"));
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
    same_->setChecked(core::colorsLinked(*m));
    const QString own = defaultColor ? defaultColor() : QString();
    const QString fallback = own.isEmpty() ? core::kModuleDefaultColor : own;
    const QString o = core::moduleColor(*m, core::ModuleColorPart::Outline, fallback);
    const QString n = core::moduleColor(*m, core::ModuleColorPart::Name, fallback);
    outline_->setIcon(swatch(o));
    outline_->setText(o);
    name_->setIcon(swatch(n));
    name_->setText(n);
    reset_->setEnabled(core::hasCustomColors(*m));
    refreshing_ = false;
}

void ModuleLookDialog::pickColor(bool outline, const QColor& c) {
    const auto* m = current_();
    if (!m || !c.isValid()) return;
    apply_(core::withColor(*m, outline ? core::ModuleColorPart::Outline : core::ModuleColorPart::Name, c.name()),
           tr("Module colors"));
    refresh();
}

void ModuleLookDialog::chooseColor(bool outline) {
    const auto* m = current_();
    if (!m) return;
    const QString own = defaultColor ? defaultColor() : QString();
    const QColor start(core::moduleColor(*m, outline ? core::ModuleColorPart::Outline : core::ModuleColorPart::Name,
                                          own.isEmpty() ? core::kModuleDefaultColor : own));
    const QColor c = QColorDialog::getColor(start, this, outline ? tr("Outline color") : tr("Name color"));
    pickColor(outline, c);
}

}  // namespace bld::ui
