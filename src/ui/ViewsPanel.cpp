#include "ViewsPanel.h"

#include "SavedViews.h"
#include "theme/Icons.h"

#include "../core/Layer.h"
#include "../core/Map.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include <cmath>

namespace bld::ui {

namespace {

// A view's row, as one button: its name in bold over a muted one-line
// summary, and "On screen now · click to stop" while it shows (the web says
// "tap"; on a computer it is a click).
class ViewRowButton : public QAbstractButton {
public:
    ViewRowButton(QString name, QString summary, bool active, QWidget* parent)
        : QAbstractButton(parent), name_(std::move(name)), summary_(std::move(summary)), active_(active) {
        setText(name_);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(name_);
        setAccessibleDescription(active_ ? summary_ + QStringLiteral(". ") + onScreenText() : summary_);
    }

    static QString onScreenText() { return ViewsPanel::tr("On screen now · click to stop"); }

    QSize sizeHint() const override {
        const int lines = boldMetrics().height() + smallMetrics().height() + 2
                          + (active_ ? smallMetrics().height() + 2 : 0);
        return { 120, std::max(lines + 2 * kPadY, 40) };
    }
    QSize minimumSizeHint() const override { return { 60, sizeHint().height() }; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (underMouse() && !active_) {
            p.setPen(Qt::NoPen);
            p.setBrush(palette().color(QPalette::AlternateBase));
            p.drawRoundedRect(card, 6, 6);
        }
        if (hasFocus()) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card, 6, 6);
        }
        const QRect r = rect().adjusted(kPadX, kPadY, -kPadX, -kPadY);
        int y = r.top();
        const auto line = [&](const QFont& f, const QColor& c, const QString& text) {
            const QFontMetrics fm(f);
            p.setFont(f);
            p.setPen(c);
            p.drawText(QRect(r.left(), y, r.width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(text, Qt::ElideRight, r.width()));
            y += fm.height() + 2;
        };
        line(boldFont(), palette().color(QPalette::Text), name_);
        line(smallFont(), palette().color(QPalette::PlaceholderText), summary_);
        if (active_) {
            QFont f = smallFont();
            f.setBold(true);
            line(f, palette().color(QPalette::Link), onScreenText());
        }
    }

private:
    static constexpr int kPadX = 10;
    static constexpr int kPadY = 6;
    QFont boldFont() const {
        QFont f = font();
        f.setBold(true);
        return f;
    }
    QFont smallFont() const {
        QFont f = font();
        f.setPointSizeF(std::max(7.0, f.pointSizeF() * 0.9));
        return f;
    }
    QFontMetrics boldMetrics() const { return QFontMetrics(boldFont()); }
    QFontMetrics smallMetrics() const { return QFontMetrics(smallFont()); }

    QString name_;
    QString summary_;
    bool active_;
};

// A small flat button on a view's row (share, Edit).
QToolButton* rowTool(QWidget* parent, const QString& objectName) {
    auto* b = new QToolButton(parent);
    b->setObjectName(objectName);
    b->setProperty("viewRowTool", true);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    return b;
}

// Gone from the panel now (so nothing finds it), deleted once the event
// loop runs: it may be the very button whose click asked for this.
void retire(QWidget* w) {
    w->hide();
    w->setParent(nullptr);
    w->deleteLater();
}

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

    // The views, one card each.
    scroll_ = new QScrollArea(host);
    scroll_->setObjectName(QStringLiteral("ViewList"));
    scroll_->setAccessibleName(tr("Saved views"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rowsHost_ = new QWidget(scroll_);
    rowsHost_->setBackgroundRole(QPalette::Window);
    rowsCol_ = new QVBoxLayout(rowsHost_);
    rowsCol_->setContentsMargins(0, 0, 0, 0);
    rowsCol_->setSpacing(6);
    rowsCol_->addStretch(1);
    scroll_->setWidget(rowsHost_);
    scroll_->setMinimumHeight(60);
    col->addWidget(scroll_, 1);

    // The open view's settings, shown in its card ("Edit").
    options_ = new QFrame(host);
    options_->setObjectName(QStringLiteral("ViewOptions"));
    options_->hide();
    auto* opt = new QVBoxLayout(options_);
    opt->setContentsMargins(10, 8, 10, 10);
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
    auto* deleteBtn = new QPushButton(tr("Delete"), options_);
    deleteBtn->setObjectName(QStringLiteral("viewDelete"));
    auto* doneBtn = new QPushButton(tr("Done"), options_);
    doneBtn->setObjectName(QStringLiteral("viewDone"));
    doneBtn->setProperty("accent", true);
    doneBtn->setToolTip(tr("Close the settings for this view"));
    editRow->addWidget(renameBtn);
    editRow->addWidget(deleteBtn);
    editRow->addStretch(1);
    editRow->addWidget(doneBtn);
    opt->addLayout(editRow);

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
        QString text = QInputDialog::getText(this, title, tr("Name:"), QLineEdit::Normal, name, &ok);
        if (!ok) return std::nullopt;
        return text;
    };

    connect(fitBtn_, &QPushButton::clicked, this, [this] { setFit(openId_, true); });
    connect(areaBtn_, &QPushButton::clicked, this, [this] { setFit(openId_, false); });
    connect(gridChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (!updating_) setGrid(openId_, on);
    });
    connect(labelsChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (!updating_) setLabels(openId_, on);
    });
    connect(allSheetsChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_) return;
        if (on) {
            setSheets(openId_, std::nullopt);
            return;
        }
        QStringList all;
        for (const auto& [id, name] : sheetList()) all << id;
        setSheets(openId_, all);
    });
    connect(renameBtn, &QPushButton::clicked, this, [this] {
        const QString id = openId_;
        const auto* v = find(id);
        if (!v) return;
        const QString was = v->name;
        if (const auto name = askName_(tr("Rename view"), was)) renameView(id, *name);
    });
    connect(deleteBtn, &QPushButton::clicked, this, [this] { deleteView(openId_); });
    connect(doneBtn, &QPushButton::clicked, this, [this] { openView({}); });
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
    if (!find(openId_)) openId_.clear();
    rebuildList();
}

void ViewsPanel::setActiveView(const QString& id) {
    if (id == activeId_) return;
    activeId_ = id;
    rebuildList();
}

void ViewsPanel::openView(const QString& id) {
    openId_ = find(id) ? id : QString();
    rebuildList();
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
    // The settings live on; the cards go (one may hold the button whose
    // click got us here, so they go once the event loop runs).
    options_->hide();
    options_->setParent(widget());
    for (QFrame* card : cards_) retire(card);
    cards_.clear();

    const int sheets = static_cast<int>(sheetList().size());
    int at = 0;
    for (const auto& v : views_) {
        const QString id = v.id;
        const QString name = v.name.isEmpty() ? tr("View") : v.name;
        const bool active = id == activeId_;
        const bool open = id == openId_;

        auto* card = new QFrame(rowsHost_);
        card->setObjectName(QStringLiteral("viewCard:") + id);
        card->setProperty("viewCard", true);
        card->setProperty("active", active);
        card->setFrameShape(QFrame::StyledPanel);
        auto* cardCol = new QVBoxLayout(card);
        cardCol->setContentsMargins(2, 2, 2, 2);
        cardCol->setSpacing(0);
        auto* head = new QHBoxLayout();
        head->setSpacing(2);

        auto* row = new ViewRowButton(name, views::viewSummary(v, sheets), active, card);
        row->setObjectName(QStringLiteral("viewRow:") + id);
        row->setToolTip(active ? tr("Showing %1. Click again to stop").arg(name) : tr("Show %1").arg(name));
        connect(row, &QAbstractButton::clicked, this, [this, id] {
            const auto* cur = find(id);
            if (!cur) return;
            if (id == activeId_) {
                emit leaveViewRequested();
                return;
            }
            const core::SavedView copy = *cur;  // the handler may change the list
            emit goToViewRequested(copy);
        });
        head->addWidget(row, 1);

        auto* share = rowTool(card, QStringLiteral("viewShare:") + id);
        share->setIcon(theme::lineIcon(QStringLiteral("picture"), palette()));
        share->setIconSize(QSize(20, 20));
        share->setToolTip(tr("Share a picture"));
        share->setAccessibleName(tr("Share a picture of %1").arg(name));
        connect(share, &QToolButton::clicked, this, [this, id] { emit sharePictureRequested(id); });
        head->addWidget(share);

        auto* edit = rowTool(card, QStringLiteral("viewEdit:") + id);
        edit->setText(tr("Edit"));
        edit->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        edit->setLayoutDirection(Qt::RightToLeft);  // the chevron after the word
        edit->setIcon(theme::lineIcon(open ? QStringLiteral("chevronUp") : QStringLiteral("chevronDown"), palette()));
        edit->setIconSize(QSize(16, 16));
        edit->setCheckable(true);
        edit->setChecked(open);
        edit->setToolTip(open ? tr("Close the settings for this view") : tr("Change what this view shows"));
        edit->setAccessibleName(tr("Change %1").arg(name));
        connect(edit, &QToolButton::clicked, this, [this, id] { openView(openId_ == id ? QString() : id); });
        head->addWidget(edit);
        cardCol->addLayout(head);

        if (open) {
            options_->setParent(card);
            cardCol->addWidget(options_);
            options_->show();
        }
        rowsCol_->insertWidget(at++, card);
        card->show();  // now, not when the layout gets round to it
        cards_.push_back(card);
    }
    refreshOptions();
    empty_->setVisible(views_.empty());
    scroll_->setVisible(!views_.empty());
    showAllBtn_->setVisible(!activeId_.isEmpty());
    exportBtn_->setText(views_.empty() ? tr("Export a picture") : tr("Export all views"));
    exportBtn_->setEnabled(map_ != nullptr);
    setEnabled(map_ != nullptr);
}

void ViewsPanel::refreshOptions() {
    const core::SavedView* v = find(openId_);
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
        if (QWidget* w = it->widget()) retire(w);
        delete it;
    }
    if (v->sheets) {
        for (const auto& [id, name] : sheetList()) {
            auto* chk = new QCheckBox(name.isEmpty() ? tr("Sheet") : name, sheetsBox_);
            chk->setObjectName(QStringLiteral("viewSheet:") + id);
            chk->setChecked(v->sheets->contains(id));
            connect(chk, &QCheckBox::toggled, this, [this, sheetId = id](bool on) {
                const core::SavedView* cur = find(openId_);
                if (!cur) return;
                QStringList next;
                for (const auto& [sid, sname] : sheetList()) {
                    const bool shown = sid == sheetId ? on : (cur->sheets ? cur->sheets->contains(sid) : true);
                    if (shown) next << sid;
                }
                setSheets(openId_, next);
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
    // Asking runs an event loop, in which the list may change: hold on to
    // the name, not the view.
    const QString name = v->name;
    if (!confirm_(tr("Delete the view \"%1\"? The layout itself doesn't change.").arg(name))) return false;
    if (!find(id)) return false;
    const bool wasActive = id == activeId_;
    std::vector<core::SavedView> next;
    for (const auto& x : views_)
        if (x.id != id) next.push_back(x);
    if (openId_ == id) openId_.clear();
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
