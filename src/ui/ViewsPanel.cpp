#include "ViewsPanel.h"

#include "SavedViews.h"

#include "../core/Layer.h"
#include "../core/Map.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QUuid>
#include <QVBoxLayout>

#include <cmath>

namespace bld::ui {

namespace {

constexpr int kSummaryRole = Qt::UserRole + 1;
constexpr int kIdRole = Qt::UserRole + 2;

// A view's row: its name in bold over a muted one-line summary.
class ViewRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem o(option);
        initStyleOption(&o, index);
        const QString name = o.text;
        const bool selected = o.state & QStyle::State_Selected;
        const bool hovered = o.state & QStyle::State_MouseOver;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        // The picked view: a soft accent card, like the web's row.
        const QRectF card = QRectF(o.rect).adjusted(1.5, 1.5, -1.5, -1.5);
        if (selected) {
            QColor fill = o.palette.color(QPalette::Highlight);
            fill.setAlphaF(0.14);
            p->setPen(QPen(o.palette.color(QPalette::Highlight), 1.2));
            p->setBrush(fill);
            p->drawRoundedRect(card, 8, 8);
        } else if (hovered) {
            p->setPen(Qt::NoPen);
            p->setBrush(o.palette.color(QPalette::AlternateBase));
            p->drawRoundedRect(card, 8, 8);
        }
        const QRect r = o.rect.adjusted(12, 7, -12, -7);
        QFont bold = o.font;
        bold.setBold(true);
        p->setFont(bold);
        p->setPen(o.palette.color(QPalette::Text));
        const QFontMetrics fmBold(bold);
        p->drawText(QRect(r.left(), r.top(), r.width(), fmBold.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fmBold.elidedText(name, Qt::ElideRight, r.width()));
        QFont small = o.font;
        small.setPointSizeF(std::max(7.0, o.font.pointSizeF() * 0.9));
        p->setFont(small);
        p->setPen(o.palette.color(QPalette::PlaceholderText));
        const QFontMetrics fmSmall(small);
        p->drawText(QRect(r.left(), r.top() + fmBold.height() + 2, r.width(), fmSmall.height()),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    fmSmall.elidedText(index.data(kSummaryRole).toString(), Qt::ElideRight, r.width()));
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        QFont bold = option.font;
        bold.setBold(true);
        const int h = QFontMetrics(bold).height() + QFontMetrics(option.font).height() + 16;
        return { 120, std::max(h, 44) };
    }
};

// Two buttons side by side, one of them on (the web's segmented control).
QFrame* segmented(QWidget* parent, QPushButton* a, QPushButton* b, QButtonGroup* group) {
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("Segmented"));
    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(3, 3, 3, 3);
    row->setSpacing(2);
    for (QPushButton* btn : { a, b }) {
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        group->addButton(btn);
        row->addWidget(btn, 1);
    }
    group->setExclusive(true);
    return frame;
}

double round2(double v) { return std::round(v * 100) / 100; }

}  // namespace

ViewsPanel::ViewsPanel(QWidget* parent) : QDockWidget(tr("Views"), parent) {
    setObjectName(QStringLiteral("dock.views"));
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(8);

    empty_ = new QLabel(tr("No saved views yet. A view remembers a part of the layout, so you can show it, "
                           "or share a picture of it, in one click."),
                        host);
    empty_->setObjectName(QStringLiteral("ViewsEmpty"));
    empty_->setWordWrap(true);
    empty_->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(empty_);

    list_ = new QListWidget(host);
    list_->setObjectName(QStringLiteral("ViewList"));
    list_->setAccessibleName(tr("Saved views"));
    list_->setItemDelegate(new ViewRowDelegate(list_));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setMouseTracking(true);  // the hovered row lights up
    list_->setSpacing(1);
    list_->setMinimumHeight(60);
    col->addWidget(list_, 1);

    // The picked view's options.
    options_ = new QFrame(host);
    options_->setObjectName(QStringLiteral("ViewOptions"));
    auto* opt = new QVBoxLayout(options_);
    opt->setContentsMargins(10, 10, 10, 10);
    opt->setSpacing(6);
    fitBtn_ = new QPushButton(tr("Fit whole layout"), options_);
    fitBtn_->setObjectName(QStringLiteral("viewFit"));
    fitBtn_->setToolTip(tr("The picture always covers the whole layout, even after it changes"));
    areaBtn_ = new QPushButton(tr("Use this area"), options_);
    areaBtn_->setObjectName(QStringLiteral("viewArea"));
    areaBtn_->setToolTip(tr("Keep the part of the map on screen now"));
    opt->addWidget(segmented(options_, fitBtn_, areaBtn_, new QButtonGroup(options_)));
    areaHint_ = new QLabel(tr("Move the map to the part you want, then click “Use this area” again to keep it."),
                           options_);
    areaHint_->setWordWrap(true);
    areaHint_->setForegroundRole(QPalette::PlaceholderText);
    opt->addWidget(areaHint_);
    gridChk_ = new QCheckBox(tr("Show the grid"), options_);
    gridChk_->setObjectName(QStringLiteral("viewGrid"));
    labelsChk_ = new QCheckBox(tr("Show labels"), options_);
    labelsChk_->setObjectName(QStringLiteral("viewLabels"));
    opt->addWidget(gridChk_);
    opt->addWidget(labelsChk_);
    auto* sheetsTitle = new QLabel(tr("Sheets"), options_);
    QFont f = sheetsTitle->font();
    f.setBold(true);
    sheetsTitle->setFont(f);
    opt->addWidget(sheetsTitle);
    allSheetsChk_ = new QCheckBox(tr("All sheets"), options_);
    allSheetsChk_->setObjectName(QStringLiteral("viewAllSheets"));
    opt->addWidget(allSheetsChk_);
    sheetsBox_ = new QWidget(options_);
    sheetsCol_ = new QVBoxLayout(sheetsBox_);
    sheetsCol_->setContentsMargins(22, 0, 0, 0);
    sheetsCol_->setSpacing(4);
    opt->addWidget(sheetsBox_);
    auto* editRow = new QHBoxLayout();
    auto* renameBtn = new QPushButton(tr("Rename…"), options_);
    renameBtn->setObjectName(QStringLiteral("viewRename"));
    auto* shareBtn = new QPushButton(tr("Share picture…"), options_);
    shareBtn->setObjectName(QStringLiteral("viewShare"));
    auto* deleteBtn = new QPushButton(tr("Delete"), options_);
    deleteBtn->setObjectName(QStringLiteral("viewDelete"));
    editRow->addWidget(renameBtn);
    editRow->addWidget(shareBtn);
    editRow->addWidget(deleteBtn);
    editRow->addStretch(1);
    opt->addLayout(editRow);
    col->addWidget(options_);

    auto* addRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("+ Add view"), host);
    addBtn->setObjectName(QStringLiteral("viewAdd"));
    addBtn->setProperty("accent", true);
    addBtn->setToolTip(tr("Save a view of the whole layout; you can pick an area after"));
    showAllBtn_ = new QPushButton(tr("Show everything"), host);
    showAllBtn_->setObjectName(QStringLiteral("viewShowAll"));
    showAllBtn_->setToolTip(tr("Back to the layout as it is, with every sheet"));
    addRow->addWidget(addBtn);
    addRow->addWidget(showAllBtn_);
    addRow->addStretch(1);
    col->addLayout(addRow);

    auto* line = new QFrame(host);
    line->setFrameShape(QFrame::HLine);
    line->setForegroundRole(QPalette::Mid);
    col->addWidget(line);
    exportBtn_ = new QPushButton(host);
    exportBtn_->setObjectName(QStringLiteral("viewExportAll"));
    exportBtn_->setToolTip(tr("One picture of each view, in a folder"));
    col->addWidget(exportBtn_);

    setWidget(host);

    confirm_ = [this](const QString& q) {
        return QMessageBox::question(this, tr("Delete view"), q) == QMessageBox::Yes;
    };
    askName_ = [this](const QString& title, const QString& name) -> std::optional<QString> {
        bool ok = false;
        const QString text = QInputDialog::getText(this, title, tr("Name:"), QLineEdit::Normal, name, &ok);
        if (!ok) return std::nullopt;
        return text;
    };

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString id = item->data(kIdRole).toString();
        selectView(id);
        if (const auto* v = find(id)) emit goToViewRequested(*v);
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (updating_ || !item) return;
        selectedId_ = item->data(kIdRole).toString();
        refreshOptions();
    });
    connect(fitBtn_, &QPushButton::clicked, this, [this] { setFit(selectedId_, true); });
    connect(areaBtn_, &QPushButton::clicked, this, [this] { setFit(selectedId_, false); });
    connect(gridChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (!updating_) setGrid(selectedId_, on);
    });
    connect(labelsChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (!updating_) setLabels(selectedId_, on);
    });
    connect(allSheetsChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_) return;
        if (on) {
            setSheets(selectedId_, std::nullopt);
            return;
        }
        QStringList all;
        for (const auto& [id, name] : sheetList()) all << id;
        setSheets(selectedId_, all);
    });
    connect(renameBtn, &QPushButton::clicked, this, [this] {
        const auto* v = find(selectedId_);
        if (!v) return;
        if (const auto name = askName_(tr("Rename view"), v->name)) renameView(selectedId_, *name);
    });
    connect(shareBtn, &QPushButton::clicked, this, [this] {
        if (!selectedId_.isEmpty()) emit sharePictureRequested(selectedId_);
    });
    connect(deleteBtn, &QPushButton::clicked, this, [this] { deleteView(selectedId_); });
    connect(addBtn, &QPushButton::clicked, this, [this] {
        if (!map_) return;
        const QString suggestion = tr("View %1").arg(views_.size() + 1);
        if (const auto name = askName_(tr("Add view"), suggestion)) addView(name->trimmed().isEmpty() ? suggestion : *name);
    });
    connect(showAllBtn_, &QPushButton::clicked, this, &ViewsPanel::showEverythingRequested);
    connect(exportBtn_, &QPushButton::clicked, this, &ViewsPanel::exportAllRequested);

    setMap(nullptr);
}

void ViewsPanel::setMap(const core::Map* map) {
    map_ = map;
    views_ = map ? map->sidecar.views : std::vector<core::SavedView>{};
    if (!find(selectedId_)) selectedId_.clear();
    rebuildList();
    refreshOptions();
}

void ViewsPanel::setActiveView(const QString& id) {
    activeId_ = id;
    showAllBtn_->setVisible(!id.isEmpty());
    if (!id.isEmpty()) selectView(id);
}

void ViewsPanel::selectView(const QString& id) {
    selectedId_ = find(id) ? id : QString();
    updating_ = true;
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kIdRole).toString() == selectedId_) list_->setCurrentRow(i);
    if (selectedId_.isEmpty()) list_->clearSelection();
    updating_ = false;
    refreshOptions();
}

const core::SavedView* ViewsPanel::find(const QString& id) const {
    if (id.isEmpty()) return nullptr;
    for (const auto& v : views_)
        if (v.id == id) return &v;
    return nullptr;
}

std::vector<std::pair<QString, QString>> ViewsPanel::sheetList() const {
    std::vector<std::pair<QString, QString>> out;
    if (!map_) return out;
    for (const auto& l : map_->layers())
        if (l && l->kind() != core::LayerKind::Grid) out.emplace_back(l->guid, l->name);
    return out;
}

void ViewsPanel::rebuildList() {
    updating_ = true;
    list_->clear();
    const int sheets = static_cast<int>(sheetList().size());
    for (const auto& v : views_) {
        auto* item = new QListWidgetItem(v.name.isEmpty() ? tr("View") : v.name, list_);
        item->setData(kIdRole, v.id);
        item->setData(kSummaryRole, views::viewSummary(v, sheets));
        item->setToolTip(tr("Show %1").arg(v.name));
        if (v.id == selectedId_) list_->setCurrentItem(item);
    }
    updating_ = false;
    empty_->setVisible(views_.empty());
    list_->setVisible(!views_.empty());
    showAllBtn_->setVisible(!activeId_.isEmpty());
    exportBtn_->setText(views_.empty() ? tr("Export a picture") : tr("Export all views"));
    exportBtn_->setEnabled(map_ != nullptr);
    setEnabled(map_ != nullptr);
}

void ViewsPanel::refreshOptions() {
    const core::SavedView* v = find(selectedId_);
    options_->setVisible(v != nullptr);
    if (!v) return;
    updating_ = true;
    fitBtn_->setChecked(v->fit);
    areaBtn_->setChecked(!v->fit);
    areaHint_->setVisible(!v->fit);
    gridChk_->setChecked(v->grid);
    labelsChk_->setChecked(v->labels);
    allSheetsChk_->setChecked(!v->sheets);
    // Later: the box being toggled may be the one that asked for this.
    while (QLayoutItem* it = sheetsCol_->takeAt(0)) {
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    if (v->sheets) {
        for (const auto& [id, name] : sheetList()) {
            auto* chk = new QCheckBox(name.isEmpty() ? tr("Sheet") : name, sheetsBox_);
            chk->setObjectName(QStringLiteral("viewSheet:") + id);
            chk->setChecked(v->sheets->contains(id));
            connect(chk, &QCheckBox::toggled, this, [this, sheetId = id](bool on) {
                const core::SavedView* cur = find(selectedId_);
                if (!cur) return;
                QStringList next;
                for (const auto& [sid, sname] : sheetList()) {
                    const bool shown = sid == sheetId ? on : (cur->sheets ? cur->sheets->contains(sid) : true);
                    if (shown) next << sid;
                }
                setSheets(selectedId_, next);
            });
            sheetsCol_->addWidget(chk);
        }
    }
    sheetsBox_->setVisible(v->sheets.has_value());
    updating_ = false;
}

void ViewsPanel::edit(const QString& id, const QString& what, const std::function<void(core::SavedView&)>& change) {
    std::vector<core::SavedView> next = views_;
    for (auto& v : next) {
        if (v.id != id) continue;
        const core::SavedView before = v;
        change(v);
        if (v != before) emit viewsEdited(next, what);
        else refreshOptions();
        return;
    }
}

void ViewsPanel::addView(const QString& name) {
    if (!map_) return;
    core::SavedView v = views::newView(QUuid::createUuid().toString(QUuid::WithoutBraces), name,
                                       gridShown_ ? gridShown_() : false);
    std::vector<core::SavedView> next = views_;
    next.push_back(v);
    selectedId_ = v.id;
    emit viewsEdited(next, tr("Add view \"%1\"").arg(v.name));
    emit goToViewRequested(v);
}

void ViewsPanel::renameView(const QString& id, const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    edit(id, tr("Rename view"), [&](core::SavedView& v) { v.name = trimmed; });
}

bool ViewsPanel::deleteView(const QString& id) {
    const auto* v = find(id);
    if (!v) return false;
    if (!confirm_(tr("Delete the view \"%1\"? The layout itself doesn't change.").arg(v->name))) return false;
    const bool wasActive = id == activeId_;
    std::vector<core::SavedView> next;
    for (const auto& x : views_)
        if (x.id != id) next.push_back(x);
    const QString name = v->name;
    if (selectedId_ == id) selectedId_.clear();
    emit viewsEdited(next, tr("Delete view \"%1\"").arg(name));
    if (wasActive) emit showEverythingRequested();
    return true;
}

void ViewsPanel::setFit(const QString& id, bool fit) {
    if (fit) {
        edit(id, tr("Fit the view to the whole layout"), [](core::SavedView& v) {
            v.fit = true;
            v.rect.reset();
        });
        return;
    }
    const auto r = screenRect_ ? screenRect_() : std::nullopt;
    if (!r) {
        refreshOptions();
        return;
    }
    const QRectF area(round2(r->x()), round2(r->y()), round2(r->width()), round2(r->height()));
    edit(id, tr("Keep this area in the view"), [&](core::SavedView& v) {
        v.fit = false;
        v.rect = area;
    });
}

void ViewsPanel::setGrid(const QString& id, bool on) {
    edit(id, on ? tr("Show the grid in the view") : tr("Hide the grid in the view"),
         [on](core::SavedView& v) { v.grid = on; });
}

void ViewsPanel::setLabels(const QString& id, bool on) {
    edit(id, on ? tr("Show labels in the view") : tr("Hide labels in the view"),
         [on](core::SavedView& v) { v.labels = on; });
}

void ViewsPanel::setSheets(const QString& id, std::optional<QStringList> sheets) {
    edit(id, tr("Choose the view's sheets"), [&](core::SavedView& v) { v.sheets = sheets; });
}

// ---------------------------------------------------------------------------

ViewIndicator::ViewIndicator(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("ViewIndicator"));
    setAutoFillBackground(true);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(14, 3, 3, 3);
    row->setSpacing(4);
    label_ = new QLabel(this);
    label_->setTextFormat(Qt::RichText);
    row->addWidget(label_);
    auto* btn = new QPushButton(tr("Show everything"), this);
    btn->setObjectName(QStringLiteral("indicatorShowAll"));
    btn->setCursor(Qt::PointingHandCursor);
    row->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, &ViewIndicator::showEverythingRequested);
    parent->installEventFilter(this);
    hide();
}

void ViewIndicator::setViewName(const QString& name) {
    if (name.isEmpty()) {
        hide();
        return;
    }
    label_->setText(tr("Showing <b>%1</b>").arg(name.toHtmlEscaped()));
    adjustSize();
    place();
    show();
    raise();
}

QString ViewIndicator::text() const { return isVisible() ? label_->text() : QString(); }

bool ViewIndicator::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parent() && event->type() == QEvent::Resize) place();
    return false;
}

void ViewIndicator::place() {
    auto* p = parentWidget();
    if (!p) return;
    const QSize s = sizeHint();
    resize(std::min(s.width(), p->width() - 24), s.height());
    move((p->width() - width()) / 2, 10);
}

}  // namespace bld::ui
