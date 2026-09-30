#include "BudgetDialog.h"
#include "help/HelpButton.h"
#include "BudgetSession.h"

#include "../core/Map.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

BudgetDialog::BudgetDialog(core::Map& map, BudgetSession& budget, QWidget* parent)
    : QDialog(parent), map_(map), budget_(budget) {
    setWindowTitle(tr("Budget — %1").arg(budget_.displayName()));
    resize(620, 500);

    auto* vbox = new QVBoxLayout(this);
    auto* btnRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(tr("Refresh from map"), this);
    btnRow->addWidget(help::headingWithHelp(tr("Budget"), QStringLiteral("dialog.budget"), this));
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    vbox->addLayout(btnRow);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({ tr("Part"), tr("Used"), tr("Limit (blank = no limit)") });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    vbox->addWidget(table_);

    statusLabel_ = new QLabel(this);
    vbox->addWidget(statusLabel_);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    vbox->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(refreshBtn, &QPushButton::clicked, this, [this]{ rebuildTable(); });

    connect(table_, &QTableWidget::cellChanged, this, [this](int row, int col){
        if (col != 2) return;
        auto* keyItem = table_->item(row, 0);
        auto* valItem = table_->item(row, 2);
        if (!keyItem || !valItem) return;
        const QString txt = valItem->text().trimmed();
        bool ok = txt.isEmpty();
        const int v = ok ? -1 : txt.toInt(&ok);
        if (!ok || v < -1) return;
        editing_ = true;
        budget_.setLimit(keyItem->text(), v);
        editing_ = false;
        rebuildTable();
    });
    // Close with the budget (e.g. Budget > Close); follow other changes.
    connect(&budget_, &BudgetSession::changed, this, [this]{
        if (!budget_.exists()) { close(); return; }
        setWindowTitle(tr("Budget — %1").arg(budget_.displayName()));
        if (!editing_) rebuildTable();
    });
    rebuildTable();
}

void BudgetDialog::rebuildTable() {
    const auto* budget = budget_.budget();
    if (!budget) return;
    const auto usage = edit::countPartUsage(map_);

    // Budgeted parts in file order, then the other parts the map uses.
    QStringList parts;
    QSet<QString> seen;
    for (const auto& entry : budget->entries()) {
        parts << entry.first;
        seen.insert(entry.first.toUpper());
    }
    QStringList others;
    for (auto it = usage.constBegin(); it != usage.constEnd(); ++it)
        if (!seen.contains(it.key())) others << it.key();
    std::sort(others.begin(), others.end());
    parts << others;

    table_->blockSignals(true);
    table_->setRowCount(parts.size());
    int overBudget = 0;
    for (int i = 0; i < parts.size(); ++i) {
        const QString& part = parts[i];
        const int used = usage.value(part.toUpper(), 0);
        const bool hasLimit = budget->hasLimit(part);
        const int limit = budget_.limit(part);

        auto* nameItem  = new QTableWidgetItem(part);
        auto* usedItem  = new QTableWidgetItem(QString::number(used));
        auto* limitItem = new QTableWidgetItem(hasLimit ? QString::number(limit) : QString());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        usedItem->setFlags(usedItem->flags() & ~Qt::ItemIsEditable);
        const bool over = limit >= 0 && used > limit;
        if (over) {
            ++overBudget;
            for (auto* it : { nameItem, usedItem, limitItem }) it->setBackground(QColor(255, 210, 210));
        }
        table_->setItem(i, 0, nameItem);
        table_->setItem(i, 1, usedItem);
        table_->setItem(i, 2, limitItem);
    }
    table_->blockSignals(false);
    statusLabel_->setText(overBudget > 0
        ? tr("⚠ %1 part(s) over budget").arg(overBudget)
        : tr("All parts within budget"));
}

}
