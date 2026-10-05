#include "ModulesPanel.h"

#include "../core/Map.h"
#include "../core/Module.h"
#include "../core/Sidecar.h"

#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace bld::ui {

ModulesPanel::ModulesPanel(QWidget* parent)
    : QDockWidget(tr("Modules"), parent) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(2);

    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    auto* createBtn = new QPushButton(tr("Group Selection"), host);
    createBtn->setToolTip(tr("Keep the selected parts together as a module in this layout"));
    auto* importBtn = new QPushButton(tr("Import…"), host);
    importBtn->setToolTip(tr("Import a .bbm file as a module"));
    auto* saveLibBtn = new QPushButton(tr("Save to Module library"), host);
    saveLibBtn->setToolTip(tr("Save the selected module to your Module library, to insert it in other layouts"));
    saveLibBtn->setEnabled(false);
    auto* deleteBtn = new QPushButton(tr("Delete"), host);
    deleteBtn->setToolTip(tr("Delete the selected module"));
    deleteBtn->setEnabled(false);
    row->addWidget(createBtn);
    row->addWidget(importBtn);
    row->addWidget(saveLibBtn);
    row->addWidget(deleteBtn);
    row->addStretch();
    more_ = new QToolButton(host);
    more_->setText(QStringLiteral("⋯"));
    more_->setToolTip(tr("More for the selected module"));
    more_->setAccessibleName(tr("More"));
    more_->setEnabled(false);
    row->addWidget(more_);
    col->addLayout(row);

    list_ = new QListWidget(host);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    col->addWidget(list_);

    setWidget(host);

    connect(createBtn, &QPushButton::clicked, this, &ModulesPanel::createModuleRequested);
    connect(importBtn, &QPushButton::clicked, this, &ModulesPanel::importBbmRequested);
    connect(deleteBtn, &QPushButton::clicked, this, [this]{
        auto* sel = list_->currentItem();
        if (!sel) return;
        const QString id = sel->toolTip();
        if (!id.isEmpty()) emit moduleDeleteRequested(id);
    });
    connect(saveLibBtn, &QPushButton::clicked, this, [this]{
        auto* sel = list_->currentItem();
        if (!sel) return;
        const QString id = sel->toolTip();
        if (!id.isEmpty()) emit saveToLibraryRequested(id);
    });
    // Delete + Save-to-library buttons enable only when a valid module row
    // is selected.
    connect(list_, &QListWidget::currentItemChanged, this, [this, deleteBtn, saveLibBtn](QListWidgetItem* cur, QListWidgetItem*){
        const bool valid = cur && !cur->toolTip().isEmpty();
        deleteBtn->setEnabled(valid);
        saveLibBtn->setEnabled(valid);
        more_->setEnabled(valid);
    });
    connect(more_, &QToolButton::clicked, this, [this] {
        auto* sel = list_->currentItem();
        if (!sel || sel->toolTip().isEmpty()) return;
        showMenu(sel->toolTip(), more_->mapToGlobal(QPoint(0, more_->height())));
    });
    // itemClicked (not currentItemChanged) fires on EVERY click, including
    // repeat clicks on the already-current row. MainWindow treats this as
    // a toggle: first click selects the module's members, second click
    // deselects. The row still looks "current" in the list — toggling
    // doesn't change the list's current-item state, only the map's
    // scene selection.
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
        if (!item) return;
        const QString id = item->toolTip();
        if (!id.isEmpty()) emit selectMembersRequested(id);
    });

    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        if (!item) return;
        const QString id = item->toolTip();  // we stash the module id in the tooltip
        if (id.isEmpty()) return;
        showMenu(id, list_->mapToGlobal(pos));
    });
}

void ModulesPanel::showMenu(const QString& id, const QPoint& globalPos) {
    QMenu menu(this);

    const core::Module* mod = nullptr;
    if (map_)
        for (const auto& m : map_->sidecar.modules)
            if (m.id == id) mod = &m;
    const bool pinned = mod && mod->pinned;

    auto* selAct = menu.addAction(tr("Select Its Parts"));
    connect(selAct, &QAction::triggered, [this, id]{ emit selectMembersRequested(id); });
    auto* editAct = menu.addAction(editingId_ == id ? tr("Done editing") : tr("Edit module"));
    editAct->setObjectName(QStringLiteral("moduleEdit"));
    connect(editAct, &QAction::triggered, [this, id]{ emit editRequested(editingId_ == id ? QString() : id); });
    auto* pinAct = menu.addAction(pinned ? tr("Unpin") : tr("Pin in place"));
    pinAct->setObjectName(QStringLiteral("modulePin"));
    connect(pinAct, &QAction::triggered, [this, id, pinned]{ emit pinRequested(id, !pinned); });

    menu.addSeparator();
    auto* moveAct = menu.addAction(tr("Move..."));
    moveAct->setEnabled(!pinned);
    if (pinned) moveAct->setToolTip(tr("Pinned in place: unpin it to move it"));
    connect(moveAct, &QAction::triggered, [this, id]{
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Move module"));
        auto* form = new QFormLayout(&dlg);
        auto* dx = new QDoubleSpinBox(&dlg);
        dx->setRange(-10000, 10000); dx->setDecimals(2);
        auto* dy = new QDoubleSpinBox(&dlg);
        dy->setRange(-10000, 10000); dy->setDecimals(2);
        form->addRow(tr("ΔX (studs):"), dx);
        form->addRow(tr("ΔY (studs):"), dy);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted)
            emit moveRequested(id, dx->value(), dy->value());
    });
    auto* rotMenu = menu.addMenu(tr("Rotate"));
    rotMenu->setEnabled(!pinned);
    for (double deg : { -90.0, -45.0, 45.0, 90.0, 180.0 }) {
        auto* a = rotMenu->addAction(tr("%1°").arg(deg > 0 ? QStringLiteral("+") + QString::number(deg)
                                                           : QString::number(deg)));
        connect(a, &QAction::triggered, [this, id, deg]{ emit rotateRequested(id, deg); });
    }

    menu.addSeparator();
    auto* renameAct = menu.addAction(tr("Rename..."));
    connect(renameAct, &QAction::triggered, [this, id]{ emit renameRequested(id); });
    auto* showName = menu.addAction(tr("Show name"));
    showName->setCheckable(true);
    showName->setChecked(!mod || mod->showName);
    connect(showName, &QAction::toggled, [this, id](bool on){ emit showNameRequested(id, on); });
    auto* look = menu.addAction(tr("Colours..."));
    connect(look, &QAction::triggered, [this, id]{ emit lookRequested(id); });
    auto* cloneAct = menu.addAction(tr("Duplicate"));
    connect(cloneAct, &QAction::triggered, [this, id]{ emit cloneRequested(id); });

    menu.addSeparator();
    auto* saveLib = menu.addAction(tr("Save to Module library"));
    connect(saveLib, &QAction::triggered, [this, id]{ emit saveToLibraryRequested(id); });

    menu.addSeparator();
    auto* flatAct = menu.addAction(tr("Ungroup (keep the parts)"));
    connect(flatAct, &QAction::triggered, [this, id]{ emit flattenRequested(id); });
    auto* rescan = menu.addAction(tr("Update from the Module library"));
    connect(rescan, &QAction::triggered, [this, id]{ emit rescanRequested(id); });

    menu.addSeparator();
    auto* del = menu.addAction(tr("Delete module"));
    connect(del, &QAction::triggered, [this, id]{ emit moduleDeleteRequested(id); });
    menu.exec(globalPos);
}

void ModulesPanel::setMap(const core::Map* map) {
    map_ = map;
    list_->clear();
    if (!map) return;
    for (const auto& m : map->sidecar.modules) {
        QString suffix;
        if (!m.sourceFile.isEmpty()) {
            suffix = QStringLiteral(" — %1").arg(QFileInfo(m.sourceFile).fileName());
        }
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 (%2 members%3)%4")
                .arg(m.name.isEmpty() ? tr("[unnamed]") : m.name)
                .arg(m.memberIds.size())
                .arg(suffix)
                .arg(m.pinned ? QStringLiteral(" 📌") : QString()));
        item->setToolTip(m.id);
        if (!m.outlineColor.isEmpty() || !m.nameColor.isEmpty()) {
            QPixmap dot(12, 12);
            dot.fill(Qt::transparent);
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QColor(0, 0, 0, 80));
            p.setBrush(QColor(m.outlineColor.isEmpty() ? m.nameColor : m.outlineColor));
            p.drawEllipse(QRectF(1, 1, 10, 10));
            p.end();
            item->setIcon(QIcon(dot));
        }
        list_->addItem(item);
    }
    if (list_->count() == 0) {
        auto* empty = new QListWidgetItem(tr("(no modules)"));
        empty->setFlags(Qt::NoItemFlags);
        list_->addItem(empty);
    }
}

}
