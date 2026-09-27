#include "PartUsagePanel.h"
#include "BudgetSession.h"

#include "MapView.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../edit/Budget.h"
#include "../parts/PartsLibrary.h"

#include <QAction>
#include <QBrush>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

namespace bld::ui {

namespace {

constexpr int kPartKeyRole = Qt::UserRole + 1;
constexpr int kIconSize    = 40;

// Keep in sync with SceneBuilder's item-data tagging.
constexpr int kBrickDataLayerIndex = 0;
constexpr int kBrickDataGuid       = 1;
constexpr int kBrickDataKind       = 2;

QString descriptionFor(const parts::PartMetadata& meta) {
    for (const auto& d : meta.descriptions) {
        if (d.language == QStringLiteral("en")) return d.text;
    }
    if (!meta.descriptions.isEmpty()) return meta.descriptions.front().text;
    return {};
}

}  // namespace

PartUsagePanel::PartUsagePanel(parts::PartsLibrary& lib, QWidget* parent)
    : QDockWidget(tr("Used Parts"), parent), lib_(lib) {

    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);

    auto* row = new QHBoxLayout();
    filterE_ = new QLineEdit(host);
    filterE_->setPlaceholderText(
        tr("Filter by part #, description, or 'over' to show only budget-exceeded"));
    row->addWidget(filterE_, 1);
    col->addLayout(row);

    table_ = new QTableWidget(host);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        QStringList{ tr("Part"), tr("Count"), tr("Description"),
                     tr("Budget"), tr("Over") });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->setIconSize(QSize(kIconSize, kIconSize));
    table_->setSortingEnabled(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    col->addWidget(table_);

    summary_ = new QLabel(host);
    summary_->setStyleSheet(QStringLiteral("color: #606060;"));
    col->addWidget(summary_);

    setWidget(host);

    // Double-click (or context-menu action) selects every brick of
    // that part on the map, so the user can jump straight to them.
    connect(table_, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*){ selectAllBricksOfCurrentRow(); });

    selectAllOfPartAct_ = new QAction(tr("Select All of This Part"), this);
    connect(selectAllOfPartAct_, &QAction::triggered,
            this, &PartUsagePanel::selectAllBricksOfCurrentRow);
    addAction(selectAllOfPartAct_);
    table_->addAction(selectAllOfPartAct_);
    table_->setContextMenuPolicy(Qt::ActionsContextMenu);

    connect(filterE_, &QLineEdit::textChanged, this,
            [this](const QString&){ refresh(); });
}

void PartUsagePanel::bindMapView(MapView* view) {
    if (mapView_) {
        disconnect(mapView_->undoStack(), nullptr, this, nullptr);
        disconnect(mapView_, nullptr, this, nullptr);
    }
    mapView_ = view;
    if (mapView_) {
        // Undo / redo / any edit that touches the map.
        connect(mapView_->undoStack(), &QUndoStack::indexChanged,
                this, [this](int){ refresh(); });
        // File-open replaces the map entirely — undoStack::indexChanged
        // doesn't fire if the stack was already empty, so also listen
        // to selectionChanged + layersChanged, which MapView::loadMap
        // emits on every successful load. Layers-changed also covers
        // add/delete layer.
        connect(mapView_, &MapView::selectionChanged,
                this, [this]{ refresh(); });
        connect(mapView_, &MapView::layersChanged,
                this, [this]{ refresh(); });
    }
    refresh();
}

QString PartUsagePanel::filterText() const {
    return filterE_ ? filterE_->text().trimmed().toLower() : QString();
}

void PartUsagePanel::setBudget(BudgetSession* budget) {
    budget_ = budget;
    connect(budget_, &BudgetSession::changed, this, &PartUsagePanel::refresh);
    refresh();
}

void PartUsagePanel::refresh() {
    if (!table_) return;
    table_->setSortingEnabled(false);
    table_->clearContents();

    auto* map = mapView_ ? mapView_->currentMap() : nullptr;
    if (!map) {
        table_->setRowCount(0);
        if (summary_) summary_->setText(tr("No project open."));
        return;
    }

    const auto usage = edit::countPartUsage(*map);
    // With a budget open, the panel also warns on over-budget parts.
    const edit::Budget* budget = budget_ ? budget_->budget() : nullptr;

    const QString needle = filterText();
    const bool onlyOver = (needle == QStringLiteral("over"));

    // Collect and sort part keys by count descending so the most-used
    // parts float to the top by default. Sorting is re-enabled below
    // so the user can re-sort on any column.
    QList<QString> keys = usage.keys();
    std::sort(keys.begin(), keys.end(),
              [&usage](const QString& a, const QString& b){
                  const int ca = usage.value(a), cb = usage.value(b);
                  if (ca != cb) return ca > cb;
                  return a.toLower() < b.toLower();
              });

    int totalBricks = 0;
    int overBudgetPartKinds = 0;
    int filteredRows = 0;
    table_->setRowCount(keys.size());

    for (int i = 0; i < keys.size(); ++i) {
        const QString& key = keys[i];
        const int count = usage.value(key, 0);
        totalBricks += count;
        const auto meta = lib_.metadata(key);
        const QString desc = meta ? descriptionFor(*meta) : QString();
        const int limit = budget ? budget_->limit(key) : -1;
        const bool hasLimit = limit >= 0;
        const int over = hasLimit ? std::max(0, count - limit) : 0;
        if (over > 0) ++overBudgetPartKinds;

        // Filter
        if (onlyOver) {
            if (over <= 0) continue;
        } else if (!needle.isEmpty()) {
            const QString hay = (key + QLatin1Char(' ') + desc).toLower();
            if (!hay.contains(needle)) continue;
        }

        auto* iconItem  = new QTableWidgetItem(key);
        iconItem->setData(kPartKeyRole, key);
        if (meta && !meta->gifFilePath.isEmpty()) {
            auto icon = iconCache_.constFind(key);
            if (icon == iconCache_.constEnd()) {
                const QPixmap pm = lib_.pixmap(key);
                icon = iconCache_.insert(key, pm.isNull() ? QIcon() : QIcon(
                    pm.scaled(kIconSize, kIconSize,
                              Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            if (!icon->isNull()) iconItem->setIcon(*icon);
        }
        auto* countItem = new QTableWidgetItem;
        countItem->setData(Qt::DisplayRole, count);
        countItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto* descItem  = new QTableWidgetItem(desc);
        auto* budgetItem = new QTableWidgetItem(hasLimit ? QString::number(limit) : QString());
        budgetItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* overItem = new QTableWidgetItem;
        if (over > 0) {
            overItem->setData(Qt::DisplayRole, over);
            overItem->setForeground(QBrush(QColor(170, 40, 40)));
            // Pink row background for the over-budget case.
            const QColor bg(255, 220, 220);
            for (auto* it : { iconItem, countItem, descItem, budgetItem, overItem })
                it->setBackground(bg);
        }
        overItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        table_->setItem(filteredRows, 0, iconItem);
        table_->setItem(filteredRows, 1, countItem);
        table_->setItem(filteredRows, 2, descItem);
        table_->setItem(filteredRows, 3, budgetItem);
        table_->setItem(filteredRows, 4, overItem);
        ++filteredRows;
    }
    table_->setRowCount(filteredRows);
    table_->resizeRowsToContents();
    table_->setSortingEnabled(true);

    if (summary_) {
        if (overBudgetPartKinds > 0) {
            summary_->setText(tr("%1 distinct part(s), %2 brick(s) total — %3 kind(s) over budget")
                                  .arg(keys.size()).arg(totalBricks).arg(overBudgetPartKinds));
        } else {
            summary_->setText(tr("%1 distinct part(s), %2 brick(s) total")
                                  .arg(keys.size()).arg(totalBricks));
        }
    }
}

void PartUsagePanel::selectAllBricksOfCurrentRow() {
    if (!mapView_) return;
    const int row = table_->currentRow();
    if (row < 0) return;
    auto* keyItem = table_->item(row, 0);
    if (!keyItem) return;
    const QString key = keyItem->data(kPartKeyRole).toString();
    if (key.isEmpty()) return;

    mapView_->deselectAll();
    const QString keyLower = key.toLower();
    auto* scene = mapView_->scene();
    if (!scene) return;
    int selected = 0;
    for (QGraphicsItem* it : scene->items()) {
        if (it->data(kBrickDataKind).toString() != QStringLiteral("brick")) continue;
        const int layer = it->data(kBrickDataLayerIndex).toInt();
        const QString guid = it->data(kBrickDataGuid).toString();
        const auto* map = mapView_->currentMap();
        if (!map || layer < 0 || layer >= static_cast<int>(map->layers().size())) continue;
        auto* L = map->layers()[layer].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& BL = static_cast<const core::LayerBrick&>(*L);
        for (const auto& b : BL.bricks) {
            if (b.guid == guid && b.partNumber.toLower() == keyLower) {
                it->setSelected(true);
                ++selected;
                break;
            }
        }
    }
    if (selected > 0) {
        // Frame them.
        const QRectF bbox = scene->selectionArea().boundingRect().isEmpty()
            ? mapView_->scene()->itemsBoundingRect()
            : scene->selectionArea().boundingRect();
        (void)bbox;  // left for a future "fit selection" hook
    }
}

}  // namespace bld::ui
