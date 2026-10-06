#include "LayerPanel.h"

#include "../core/Layer.h"
#include "../core/Map.h"
#include "../rendering/SceneBuilder.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace bld::ui {

namespace {

// The file format's name for the kind, shown in the tooltip next to the
// friendly one so the technical detail stays findable.
const char* layerKindName(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return "grid";
        case core::LayerKind::Brick:        return "brick";
        case core::LayerKind::Text:         return "text";
        case core::LayerKind::Area:         return "area";
        case core::LayerKind::Ruler:        return "ruler";
        case core::LayerKind::AnchoredText: return "anchored";
    }
    return "?";
}

// Compact per-kind symbol so the user can tell sheet types apart at a
// glance, drawn as the row's icon. Unicode symbols keep this free of icon
// resources.
QString layerKindGlyph(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return QStringLiteral("◫");
        case core::LayerKind::Brick:        return QStringLiteral("▦");
        case core::LayerKind::Text:         return QStringLiteral("A");
        case core::LayerKind::Area:         return QStringLiteral("▣");
        case core::LayerKind::Ruler:        return QStringLiteral("⟷");
        case core::LayerKind::AnchoredText: return QStringLiteral("↳");
    }
    return QStringLiteral("?");
}

constexpr int kKindRole = Qt::UserRole + 1;

// The kind's symbol in the list's text colour, so it suits light and dark.
QIcon kindIcon(core::LayerKind k, const QWidget* list) {
    const qreal dpr = list->devicePixelRatioF();
    const int side = 16;
    QPixmap pm(QSize(side, side) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f = list->font();
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(list->palette().color(QPalette::Text));
    p.drawText(QRect(0, 0, side, side), Qt::AlignCenter, layerKindGlyph(k));
    p.end();
    return QIcon(pm);
}

}  // namespace

QString LayerPanel::friendlyKindName(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return tr("Grid sheet");
        case core::LayerKind::Brick:        return tr("Parts sheet");
        case core::LayerKind::Text:         return tr("Text sheet");
        case core::LayerKind::Area:         return tr("Area sheet");
        case core::LayerKind::Ruler:        return tr("Ruler sheet");
        case core::LayerKind::AnchoredText: return tr("Label sheet");
    }
    return tr("Sheet");
}

LayerPanel::LayerPanel(QWidget* parent) : QDockWidget(tr("Sheets"), parent) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(2);

    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    auto* showAll = new QPushButton(tr("Show all"), host);
    auto* hideOthers = new QPushButton(tr("Solo"), host);
    hideOthers->setToolTip(tr("Show only the selected sheet"));
    auto* addBtn = new QPushButton(tr("+"), host);
    addBtn->setToolTip(tr("Add a new sheet"));
    auto* delBtn = new QPushButton(tr("−"), host);
    delBtn->setToolTip(tr("Delete the selected sheet"));
    auto* upBtn  = new QPushButton(tr("▲"), host);
    upBtn->setToolTip(tr("Move selected sheet up"));
    auto* dnBtn  = new QPushButton(tr("▼"), host);
    dnBtn->setToolTip(tr("Move selected sheet down"));
    row->addWidget(showAll);
    row->addWidget(hideOthers);
    row->addStretch();
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addWidget(upBtn);
    row->addWidget(dnBtn);
    col->addLayout(row);

    connect(addBtn, &QPushButton::clicked, this, [this]{
        QMenu m(this);
        auto add = [&](const QString& label, core::LayerKind k){
            auto* a = m.addAction(label);
            connect(a, &QAction::triggered, [this, k]{ emit addLayerRequested(k); });
        };
        add(tr("Grid sheet"),  core::LayerKind::Grid);
        add(tr("Parts sheet"), core::LayerKind::Brick);
        add(tr("Text sheet"),  core::LayerKind::Text);
        add(tr("Area sheet"),  core::LayerKind::Area);
        add(tr("Ruler sheet"), core::LayerKind::Ruler);
        m.exec(QCursor::pos());
    });
    connect(delBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit deleteLayerRequested(row);
    });
    connect(upBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit moveLayerRequested(row, +1);
    });
    connect(dnBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit moveLayerRequested(row, -1);
    });

    list_ = new QListWidget(host);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->installEventFilter(this);
    col->addWidget(list_);

    setWidget(host);

    connect(showAll, &QPushButton::clicked, this, [this]{
        for (int i = 0; i < list_->count(); ++i) list_->item(i)->setCheckState(Qt::Checked);
    });
    connect(hideOthers, &QPushButton::clicked, this, [this]{
        auto* sel = list_->currentItem();
        if (!sel) return;
        for (int i = 0; i < list_->count(); ++i) {
            list_->item(i)->setCheckState(list_->item(i) == sel ? Qt::Checked : Qt::Unchecked);
        }
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!builder_) return;
        const int idx = list_->row(item);
        const bool visible = item->checkState() == Qt::Checked;
        builder_->setLayerVisible(idx, visible);
        // Also persist on the underlying Layer so a later scene rebuild
        // (triggered by e.g. a move / undo / paste) honours the user's
        // visibility choice. Without this, any rebuildScene call would
        // read the Layer's default visible=true and make hidden layers
        // reappear despite the checkbox still showing unchecked.
        if (map_ && idx >= 0 && idx < static_cast<int>(map_->layers().size())) {
            map_->layers()[idx]->visible = visible;
            // Module frames fit the parts that show, dashed more sparsely when some are hidden.
            builder_->refreshModuleLabels(*map_);
        }
        emit layerVisibilityChanged(idx, visible);
    });
    // Clicking (or arrow-keying) onto a row sets it as the active layer —
    // vanilla BlueBrick uses Map.selectedLayerIndex for new-item placements.
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row){
        if (row >= 0 && map_) {
            map_->selectedLayerIndex = row;
            emit activeLayerChanged(row);
            // Re-render the panel so the active row renders bold.
            setMap(map_, builder_);
        }
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item){
        if (item) emit layerOptionsRequested(list_->row(item));
    });

    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        QMenu menu(this);

        if (item) {
            const int row = list_->row(item);
            const bool visible = (item->checkState() == Qt::Checked);
            auto* setActive = menu.addAction(tr("Make Active Sheet"));
            connect(setActive, &QAction::triggered, [this, row]{
                list_->setCurrentRow(row);
            });
            menu.addSeparator();
            auto* toggle = menu.addAction(visible ? tr("Hide") : tr("Show"));
            connect(toggle, &QAction::triggered, [item, visible]{
                item->setCheckState(visible ? Qt::Unchecked : Qt::Checked);
            });
            auto* allOn = menu.addAction(tr("Show all sheets"));
            connect(allOn, &QAction::triggered, [this]{
                for (int i = 0; i < list_->count(); ++i) list_->item(i)->setCheckState(Qt::Checked);
            });
            auto* allOff = menu.addAction(tr("Hide all other sheets"));
            connect(allOff, &QAction::triggered, [this, item]{
                for (int i = 0; i < list_->count(); ++i) {
                    list_->item(i)->setCheckState(list_->item(i) == item ? Qt::Checked : Qt::Unchecked);
                }
            });
            menu.addSeparator();
            auto* opts = menu.addAction(tr("Sheet options..."));
            connect(opts, &QAction::triggered, [this, row]{ emit layerOptionsRequested(row); });
            auto* ren = menu.addAction(tr("Rename..."));
            connect(ren, &QAction::triggered, [this, row, item]{
                bool ok = false;
                // The sheet's own name (the row may show "(untitled)").
                const QString def = item->data(Qt::UserRole).toString();
                const QString name = QInputDialog::getText(
                    this, tr("Rename sheet"), tr("Sheet name:"),
                    QLineEdit::Normal, def, &ok);
                if (ok && !name.isEmpty()) emit renameLayerRequested(row, name);
            });
            auto* up = menu.addAction(tr("Move Up"));
            connect(up, &QAction::triggered, [this, row]{ emit moveLayerRequested(row, +1); });
            auto* dn = menu.addAction(tr("Move Down"));
            connect(dn, &QAction::triggered, [this, row]{ emit moveLayerRequested(row, -1); });
            menu.addSeparator();
            auto* del = menu.addAction(tr("Delete sheet"));
            connect(del, &QAction::triggered, [this, row]{ emit deleteLayerRequested(row); });
            menu.addSeparator();
        }

        // "Add Layer" submenu — available whether or not a row is under the cursor.
        auto* addMenu = menu.addMenu(tr("Add Sheet"));
        auto addKind = [&](const QString& label, core::LayerKind k){
            auto* a = addMenu->addAction(label);
            connect(a, &QAction::triggered, [this, k]{ emit addLayerRequested(k); });
        };
        addKind(tr("Grid sheet"),  core::LayerKind::Grid);
        addKind(tr("Parts sheet"), core::LayerKind::Brick);
        addKind(tr("Text sheet"),  core::LayerKind::Text);
        addKind(tr("Area sheet"),  core::LayerKind::Area);
        addKind(tr("Ruler sheet"), core::LayerKind::Ruler);

        menu.exec(list_->mapToGlobal(pos));
    });
}

bool LayerPanel::eventFilter(QObject* watched, QEvent* e) {
    // Redraw the kind icons in the list's new text colour (light / dark).
    if (watched == list_ && e->type() == QEvent::PaletteChange) {
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* item = list_->item(i);
            item->setIcon(kindIcon(static_cast<core::LayerKind>(item->data(kKindRole).toInt()), list_));
        }
    }
    return QDockWidget::eventFilter(watched, e);
}

int LayerPanel::currentRow() const { return list_->currentRow(); }

void LayerPanel::setMap(core::Map* map, rendering::SceneBuilder* builder) {
    builder_ = builder;
    map_ = map;
    list_->blockSignals(true);
    list_->clear();
    if (map) {
        int i = 0;
        for (const auto& layer : map->layers()) {
            const bool isActive = (i == map->selectedLayerIndex);
            // The row shows just the sheet's name, like the web's Sheets
            // panel; the kind is the icon, and the kind, number and
            // transparency are in the tooltip.
            auto* item = new QListWidgetItem(layer->name.isEmpty() ? tr("(untitled)") : layer->name);
            item->setData(Qt::UserRole, layer->name);
            item->setData(kKindRole, static_cast<int>(layer->kind()));
            item->setIcon(kindIcon(layer->kind(), list_));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(layer->visible ? Qt::Checked : Qt::Unchecked);
            QStringList tip;
            tip << tr("%1 · #%2 · %3")
                       .arg(friendlyKindName(layer->kind()))
                       .arg(i)
                       .arg(QLatin1String(layerKindName(layer->kind())));
            if (layer->transparency < 100) tip << tr("Transparency: %1%").arg(layer->transparency);
            if (isActive) {
                QFont f = item->font(); f.setBold(true); item->setFont(f);
                item->setBackground(QColor(60, 120, 200, 60));
                tip << tr("Active sheet: new items are placed here.");
            }
            tip << tr("Double-click for sheet options.");
            item->setToolTip(tip.join(QLatin1Char('\n')));
            item->setData(Qt::AccessibleDescriptionRole, friendlyKindName(layer->kind()));
            list_->addItem(item);
            ++i;
        }
        if (map->selectedLayerIndex >= 0 && map->selectedLayerIndex < list_->count()) {
            list_->setCurrentRow(map->selectedLayerIndex);
        }
    }
    list_->blockSignals(false);
}

}
