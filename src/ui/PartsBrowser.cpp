#include "PartsBrowser.h"
#include "LoadingCard.h"
#include "BudgetSession.h"

#include "../core/Map.h"

#include "../parts/PartsLibrary.h"
#include "theme/AppPrefs.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QDrag>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineF>
#include <QNativeGestureEvent>
#include <QSlider>
#include <QWheelEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSize>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QTimer>
#include <QScroller>
#include <QTouchEvent>
#include <cmath>
#include "TouchMode.h"
#include <QWidget>

namespace bld::ui {

namespace {

// Store the part key on each item so we can retrieve it on activation.
constexpr int kPartKeyRole  = Qt::UserRole + 1;
// Store the category (derived from parent folder) for filtering.
constexpr int kCategoryRole = Qt::UserRole + 2;
// A lowercased concatenation of everything searchable for fuzzy matching.
constexpr int kFuzzyHayRole = Qt::UserRole + 3;
// The caption without budget numbers.
constexpr int kCaptionRole  = Qt::UserRole + 4;

// Local QListWidget that supplies a custom MIME payload on drag so MapView
// can identify a drop as a part-from-the-library rather than generic text.
class DraggablePartsList : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        auto* mime = new QMimeData();
        if (!items.isEmpty()) {
            const QString key = items.first()->data(kPartKeyRole).toString();
            mime->setData(QString::fromLatin1(PartsBrowser::kPartMimeType), key.toUtf8());
            mime->setText(key);  // fallback for targets that only read plain text
        }
        return mime;
    }
};

// Subsequence-based fuzzy match: every character in `needle` must appear in
// `hay` in order (not necessarily consecutive). Returns a score where higher
// is a better match; 0 means no match. The scorer rewards consecutive hits,
// start-of-string matches, and shorter overall match spans so that typing
// "plt" ranks "plate" above "specialist".
int fuzzyScore(const QString& needleLower, const QString& hayLower) {
    if (needleLower.isEmpty()) return 1;
    int hi = 0;
    int score = 0;
    int consecutive = 0;
    int firstHit = -1;
    int lastHit = -1;
    for (QChar nc : needleLower) {
        bool matched = false;
        while (hi < hayLower.size()) {
            if (hayLower[hi] == nc) {
                if (firstHit < 0) firstHit = hi;
                if (lastHit == hi - 1) consecutive++;
                else                   consecutive = 0;
                lastHit = hi;
                score += 10 + consecutive * 5;          // reward runs
                if (hi == 0) score += 15;                // reward start-of-string
                ++hi;
                matched = true;
                break;
            }
            ++hi;
        }
        if (!matched) return 0;
    }
    // Penalise how far in we had to reach to find the first hit and how
    // spread-out the matches were.
    const qsizetype nLen  = needleLower.size();
    const qsizetype spread = static_cast<qsizetype>(lastHit - firstHit) - (nLen - 1);
    if (firstHit > 0) score -= static_cast<int>(std::min<qsizetype>(firstHit, 10));
    score -= static_cast<int>(std::min<qsizetype>(spread, 10));
    return std::max(score, 1);
}

}

void PartsBrowser::setCatalogLinkVisible(bool visible) { catalogLink_->setVisible(visible); }

PartsBrowser::PartsBrowser(parts::PartsLibrary& lib, QWidget* parent)
    : QDockWidget(tr("Parts"), parent), lib_(lib) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout();
    category_ = new QComboBox(host);
    category_->setMinimumContentsLength(12);
    row->addWidget(category_, 1);

    filter_ = new QLineEdit(host);
    filter_->setPlaceholderText(tr("Fuzzy filter — e.g. \"plt2\" matches \"plate2x4\""));
    row->addWidget(filter_, 2);
    col->addLayout(row);
    catalogLink_ = new QPushButton(tr("Browse the catalog on the web…"), host);
    catalogLink_->setObjectName(QStringLiteral("browseCatalog"));
    catalogLink_->setFlat(true);
    catalogLink_->setCursor(Qt::PointingHandCursor);
    catalogLink_->setToolTip(tr("Parts people shared for everyone, on your server's website"));
    catalogLink_->setVisible(false);
    connect(catalogLink_, &QPushButton::clicked, this, &PartsBrowser::browseCatalogRequested);
    col->addWidget(catalogLink_, 0, Qt::AlignLeft);

    grid_ = new DraggablePartsList(host);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(6);
    // Enable drag so users can pick a part and drop it anywhere on the map.
    grid_->setDragEnabled(true);
    grid_->setDragDropMode(QAbstractItemView::DragOnly);
    grid_->setDefaultDropAction(Qt::CopyAction);
    grid_->setUniformItemSizes(true);
    grid_->setWordWrap(true);
    grid_->setTextElideMode(Qt::ElideRight);
    // Cell size: full icon footprint plus margin on the sides and a two-line
    // caption underneath. Uniform sizing is *off* so cells expand to whatever
    // size an individual thumbnail actually needs — extreme aspect-ratio parts
    // (e.g. 2x16 bricks) still show their full silhouette rather than getting
    // squished into a tall/thin letterbox inside a fixed square cell.
    grid_->setUniformItemSizes(false);
    loading_ = new LoadingCard(host);
    col->addWidget(loading_);
    col->addWidget(grid_);

    // Picture size: a slider under the list, Ctrl+wheel or a pinch over it.
    auto* sizeRow = new QHBoxLayout();
    auto* sizeLabel = new QLabel(tr("Picture size"), host);
    sizeRow->addWidget(sizeLabel);
    sizeSlider_ = new QSlider(Qt::Horizontal, host);
    sizeSlider_->setObjectName(QStringLiteral("partsIconSize"));
    sizeSlider_->setRange(theme::kPartsIconMin, theme::kPartsIconMax);
    sizeSlider_->setSingleStep(8);
    sizeSlider_->setPageStep(16);
    sizeSlider_->setAccessibleName(tr("Picture size"));
    sizeSlider_->setToolTip(tr("How big the part pictures are. %1+wheel or a pinch over the list also resizes them.")
                                .arg(QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText).remove(QLatin1Char('+'))));
    sizeLabel->setBuddy(sizeSlider_);
    sizeRow->addWidget(sizeSlider_, 1);
    col->addLayout(sizeRow);
    connect(sizeSlider_, &QSlider::valueChanged, this, &PartsBrowser::chooseIconSize);
    saveSize_ = new QTimer(this);
    saveSize_->setSingleShot(true);
    saveSize_->setInterval(400);
    connect(saveSize_, &QTimer::timeout, this, &PartsBrowser::saveIconSize);
    iconTimer_ = new QTimer(this);
    iconTimer_->setInterval(0);
    connect(iconTimer_, &QTimer::timeout, this, &PartsBrowser::loadSomeIcons);

    setWidget(host);

    connect(category_, &QComboBox::currentTextChanged, this, [this](const QString&) { applyFilter(); });
    connect(filter_,   &QLineEdit::textChanged,        this, [this](const QString&) { applyFilter(); });
    connect(grid_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        if (it) emit partActivated(it->data(kPartKeyRole).toString());
    });

    // Touch: tap a part, then the map; or drag it sideways out onto the
    // map; up and down flicks the grid (eventFilter).
    grid_->setProperty("bldNoTouchScroll", true);
    grid_->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    grid_->viewport()->installEventFilter(this);
    QScroller::scroller(grid_->viewport());
    grid_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(grid_, &QWidget::customContextMenuRequested, this, &PartsBrowser::showPartMenu);
    holdTimer_ = new QTimer(this);
    holdTimer_->setSingleShot(true);
    holdTimer_->setInterval(kLongPressMs);
    connect(holdTimer_, &QTimer::timeout, this, [this] {
        if (touch_ != TouchState::Undecided || touchKey_.isEmpty()) return;
        touch_ = TouchState::Held;
        if (QListWidgetItem* it = grid_->itemAt(touchStart_.toPoint())) grid_->setCurrentItem(it);
        // Queued: the menu runs its own loop, outside this timer's signal.
        const QPoint at = touchStart_.toPoint();
        QMetaObject::invokeMethod(this, [this, at] { showPartMenu(at); }, Qt::QueuedConnection);
    });

    setPrefsStore(&theme::PrefsStore::instance());
    rebuild();
}

void PartsBrowser::setPrefsStore(theme::PrefsStore* store) {
    if (prefs_) prefs_->disconnect(this);
    prefs_ = store;
    if (!prefs_) return;
    // A size chosen on another device (or the web) shows here too.
    connect(prefs_, &theme::PrefsStore::changed, this, [this] { setIconSize(prefs_->prefs().partsIconSize); });
    setIconSize(prefs_->prefs().partsIconSize);
}

void PartsBrowser::chooseIconSize(int px) {
    setIconSize(px);
    saveSize_->start();
}

void PartsBrowser::saveIconSize() {
    if (!prefs_ || prefs_->prefs().partsIconSize == iconSize_) return;
    theme::AppPrefs p = prefs_->prefs();
    p.partsIconSize = iconSize_;
    prefs_->update(p);
}

void PartsBrowser::setIconSize(int px) {
    px = theme::clampPartsIconSize(px);
    if (px == iconSize_) return;
    const bool first = iconSize_ == 0;
    iconSize_ = px;
    grid_->setIconSize(QSize(px, px));
    updateGridSize();
    {
        const QSignalBlocker quiet(sizeSlider_);
        sizeSlider_->setValue(px);
    }
    if (first || grid_->count() == 0) return;
    // Read the pictures again at the new size, the ones in view first.
    const bool loading = !iconQueue_.isEmpty();
    iconQueue_.clear();
    iconItems_.clear();
    QStringList later;
    const QRect view = grid_->viewport()->rect();
    for (int i = 0; i < grid_->count(); ++i) {
        QListWidgetItem* item = grid_->item(i);
        const QString key = item->data(kPartKeyRole).toString();
        if (iconItems_.contains(key)) continue;
        iconItems_.insert(key, item);
        (!item->isHidden() && grid_->visualItemRect(item).intersects(view) ? iconQueue_ : later) << key;
    }
    iconQueue_ << later;
    iconsDone_ = 0;
    iconsTotal_ = static_cast<int>(iconQueue_.size());
    quietIcons_ = !loading;
    iconTimer_->start();
}

void PartsBrowser::updateGridSize() {
    // Room for a two-line caption, and the budget numbers when shown.
    grid_->setGridSize(QSize(iconSize_ + 32, iconSize_ + (budgetNumbers_ ? 68 : 52)));
}

void PartsBrowser::showPartMenu(const QPoint& pos) {
    auto* it = grid_->itemAt(pos);
    if (!it) return;
    QMenu menu(this);
    const QString key = it->data(kPartKeyRole).toString();
    auto* add = menu.addAction(tr("Add '%1' to map").arg(key));
    connect(add, &QAction::triggered, [this, key]{ emit partActivated(key); });
    menu.addSeparator();
    auto* copy = menu.addAction(tr("Copy part number"));
    connect(copy, &QAction::triggered, [key]{
        QApplication::clipboard()->setText(key);
    });

    // Imports — installed by the LDraw / Studio / LDD importer
    // into a user-writable `imports/` subfolder of the configured
    // module library. Offer a delete that wipes the .xml + .gif
    // pair off disk and triggers a parts-library rescan. Skipping
    // this for vendored parts (read-only location) so users can't
    // accidentally delete BlueBrickParts entries.
    auto meta = lib_.metadata(key);
    if (meta && meta->importSource) {
        menu.addSeparator();
        auto* again = menu.addAction(tr("Re-import from Source..."));
        const bool there = QFileInfo::exists(meta->importSource->path);
        again->setEnabled(there);
        again->setToolTip(there ? meta->importSource->path
                                : tr("%1 no longer exists").arg(meta->importSource->path));
        connect(again, &QAction::triggered, this, [this, key]{ emit reimportRequested(key); });
    }
    if (meta && !meta->xmlFilePath.isEmpty()
        && meta->xmlFilePath.contains(QStringLiteral("/imports/"))) {
        menu.addSeparator();
        auto* del = menu.addAction(tr("Delete imported part..."));
        connect(del, &QAction::triggered, this, [this, key, meta]{
            const auto btn = QMessageBox::question(this,
                tr("Delete imported part"),
                tr("Delete '%1'?\n\nThis removes:\n  %2\n  %3")
                    .arg(key, meta->xmlFilePath, meta->gifFilePath),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (btn != QMessageBox::Yes) return;
            if (!meta->xmlFilePath.isEmpty()) QFile::remove(meta->xmlFilePath);
            if (!meta->gifFilePath.isEmpty()) QFile::remove(meta->gifFilePath);
            // Also clear any sibling files the importer dropped
            // alongside (PNG fallback, README, etc.) so re-importing
            // the same model doesn't pick up stale companions.
            const QFileInfo fi(meta->xmlFilePath);
            const QString stem = fi.completeBaseName();
            const QDir dir = fi.absoluteDir();
            for (const QFileInfo& sibling : dir.entryInfoList(
                    { stem + QStringLiteral(".*") }, QDir::Files)) {
                QFile::remove(sibling.absoluteFilePath());
            }
            emit partDeleted();
        });
    }

    menu.exec(grid_->mapToGlobal(pos));
}

QString PartsBrowser::categoryForPath(const QString& absPath) const {
    const QFileInfo f(absPath);
    const QString parent = f.dir().dirName();
    return parent.isEmpty() ? tr("Other") : parent;
}

namespace {

void setPartIcon(parts::PartsLibrary& lib, const QString& key, QListWidgetItem* item, int size) {
    QPixmap pm = lib.pixmap(key);
    if (!pm.isNull()) {
        item->setIcon(QIcon(pm.scaled(size, size,
                                      Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation)));
    }
}

// Build one grid item from a library entry. Shared between rebuild()
// (which calls it for every key) and addOne() (single insert after an
// import). Returns nullptr if the key isn't in the library.
QListWidgetItem* makePartItem(parts::PartsLibrary& lib,
                              const QString& key,
                              const QString& cat,
                              int iconSize = 0) {
    auto meta = lib.metadata(key);
    if (!meta) return nullptr;

    QString desc;
    for (const auto& d : meta->descriptions) {
        if (d.language == QStringLiteral("en")) { desc = d.text; break; }
    }
    if (desc.isEmpty() && !meta->descriptions.isEmpty()) {
        desc = meta->descriptions.front().text;
    }

    QString descShort = desc;
    if (descShort.size() > 28) descShort = descShort.left(27) + QChar(0x2026);
    const QString caption = descShort.isEmpty() ? key : descShort;

    auto* item = new QListWidgetItem(caption);
    item->setToolTip(desc.isEmpty() ? key : QStringLiteral("%1\n(%2)").arg(desc, key));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);

    // Go through PartsLibrary::pixmap() rather than loading meta->gifFilePath
    // directly — sets without a companion image (BrickTracks/4DBrix/TrixBrix
    // and any user-saved set) get a composite synthesized from their subparts.
    if (iconSize > 0) setPartIcon(lib, key, item, iconSize);

    item->setData(kCaptionRole,  caption);
    item->setData(kPartKeyRole,  key);
    item->setData(kCategoryRole, cat);
    item->setData(kFuzzyHayRole, (key + QLatin1Char(' ') + desc).toLower());
    return item;
}

}  // namespace

void PartsBrowser::rebuild() {
    iconTimer_->stop();
    iconQueue_.clear();
    iconItems_.clear();
    iconsDone_ = iconsTotal_ = 0;
    quietIcons_ = false;
    loading_->finish();
    grid_->clear();
    const QString previousCat = category_->currentText();
    category_->blockSignals(true);
    category_->clear();
    category_->addItem(tr("All categories"));
    QSet<QString> cats;

    const auto keys = lib_.keys();
    for (const QString& key : keys) {
        auto meta = lib_.metadata(key);
        if (!meta) continue;
        const QString cat = categoryForPath(meta->xmlFilePath);
        cats.insert(cat);
        if (auto* item = makePartItem(lib_, key, cat)) {
            grid_->addItem(item);
        }
    }

    // Sort category list and re-select what was active.
    QStringList sortedCats = cats.values();
    std::sort(sortedCats.begin(), sortedCats.end());
    category_->addItems(sortedCats);
    const int restoreIdx = category_->findText(previousCat);
    category_->setCurrentIndex(restoreIdx > 0 ? restoreIdx : 0);
    category_->blockSignals(false);

    grid_->sortItems(Qt::AscendingOrder);
    refreshBudget();

    // Thumbnails next, a few at a time, in the order the grid shows them.
    for (int i = 0; i < grid_->count(); ++i) {
        QListWidgetItem* item = grid_->item(i);
        const QString key = item->data(kPartKeyRole).toString();
        if (iconItems_.contains(key)) continue;
        iconItems_.insert(key, item);
        iconQueue_ << key;
    }
    iconsTotal_ = static_cast<int>(iconQueue_.size());
    if (iconsTotal_ > 0) iconTimer_->start();
}

void PartsBrowser::loadSomeIcons() {
    QElapsedTimer clock;
    clock.start();
    while (!iconQueue_.isEmpty() && clock.elapsed() < 15) {
        const QString key = iconQueue_.takeFirst();
        if (QListWidgetItem* item = iconItems_.take(key)) setPartIcon(lib_, key, item, iconSize_);
        ++iconsDone_;
    }
    if (iconQueue_.isEmpty()) {
        iconTimer_->stop();
        loading_->finish();
        quietIcons_ = false;
    } else if (!quietIcons_) {
        loading_->showProgress(tr("Loading part pictures…"), iconsDone_, iconsTotal_);
    }
}

void PartsBrowser::addOne(const QString& key) {
    auto meta = lib_.metadata(key);
    if (!meta) return;
    // Replace an existing entry for this key (a re-imported part gets a
    // new sprite) rather than listing it twice.
    for (int i = 0; i < grid_->count(); ++i) {
        if (grid_->item(i)->data(kPartKeyRole).toString() == key) {
            iconItems_.remove(key);
            delete grid_->takeItem(i);
            break;
        }
    }
    const QString cat = categoryForPath(meta->xmlFilePath);
    // Add the category to the dropdown if it's new. blockSignals so the
    // category-changed handler doesn't trigger applyFilter() mid-add.
    if (category_->findText(cat) < 0) {
        category_->blockSignals(true);
        category_->addItem(cat);
        // Keep the dropdown sorted (alphabetical, with "All categories"
        // sticky at index 0).
        QStringList items;
        for (int i = 1; i < category_->count(); ++i) items << category_->itemText(i);
        std::sort(items.begin(), items.end());
        const QString prev = category_->currentText();
        while (category_->count() > 1) category_->removeItem(1);
        category_->addItems(items);
        const int restoreIdx = category_->findText(prev);
        category_->setCurrentIndex(restoreIdx >= 0 ? restoreIdx : 0);
        category_->blockSignals(false);
    }
    if (auto* item = makePartItem(lib_, key, cat, iconSize_)) {
        grid_->addItem(item);
        grid_->sortItems(Qt::AscendingOrder);
        refreshBudget();
    }
}

void PartsBrowser::setBudget(BudgetSession* budget, std::function<const core::Map*()> map) {
    budget_ = budget;
    map_ = std::move(map);
    connect(budget_, &BudgetSession::changed, this, &PartsBrowser::refreshBudget);
    refreshBudget();
}

void PartsBrowser::refreshBudget() {
    const bool numbers = budget_ && budget_->exists() && budget_->showBudgetNumbers();
    const core::Map* map = map_ ? map_() : nullptr;
    const auto usage = numbers && map ? edit::countPartUsage(*map) : QHash<QString, int>{};
    budgetNumbers_ = numbers;
    updateGridSize();
    for (int i = 0; i < grid_->count(); ++i) {
        auto* it = grid_->item(i);
        const QString caption = it->data(kCaptionRole).toString();
        if (!numbers) {
            if (it->text() != caption) it->setText(caption);
            it->setData(Qt::BackgroundRole, QVariant());
            continue;
        }
        const QString key = it->data(kPartKeyRole).toString();
        const int used = usage.value(key.toUpper(), 0);
        const int limit = budget_->limit(key);
        it->setText(QStringLiteral("%1\n%2/%3").arg(caption).arg(used)
                        .arg(limit >= 0 ? QString::number(limit) : QStringLiteral("?")));
        if (limit >= 0 && used > limit) it->setBackground(QColor(255, 120, 120));
        else it->setData(Qt::BackgroundRole, QVariant());
    }
    applyFilter();
}

void PartsBrowser::applyFilter() {
    const QString needle = filter_->text().trimmed().toLower();
    const QString cat = category_->currentText();
    const bool allCats = (category_->currentIndex() <= 0);

    // First pass: filter by category and compute a fuzzy score. An empty filter
    // keeps every item visible with score 1. Items with score 0 get hidden.
    std::vector<std::pair<int, QListWidgetItem*>> scored;  // (score, item)
    for (int i = 0; i < grid_->count(); ++i) {
        auto* it = grid_->item(i);
        const QString itemCat = it->data(kCategoryRole).toString();
        const bool catOk = allCats || (itemCat == cat);
        if (!catOk) { it->setHidden(true); continue; }
        const int score = fuzzyScore(needle, it->data(kFuzzyHayRole).toString());
        if (score <= 0) { it->setHidden(true); continue; }
        if (budget_ && budget_->exists() && budget_->showOnlyBudgetedParts()
            && !budget_->isBudgeted(it->data(kPartKeyRole).toString())) { it->setHidden(true); continue; }
        it->setHidden(false);
        scored.emplace_back(score, it);
    }

    // Sort the visible items so best fuzzy matches show first; within equal
    // scores keep alphabetical order by part key for stability.
    if (!needle.isEmpty() && !scored.empty()) {
        grid_->setSortingEnabled(false);
        std::stable_sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) {
                if (a.first != b.first) return a.first > b.first;
                return a.second->data(kPartKeyRole).toString()
                     < b.second->data(kPartKeyRole).toString();
            });
        // Re-order by removing and reinserting in score order.
        for (size_t i = 0; i < scored.size(); ++i) {
            const int currentRow = grid_->row(scored[i].second);
            if (currentRow != static_cast<int>(i)) {
                auto* taken = grid_->takeItem(currentRow);
                grid_->insertItem(static_cast<int>(i), taken);
            }
        }
    } else {
        // Re-enable alphabetical ordering when the filter is empty.
        grid_->setSortingEnabled(true);
        grid_->sortItems(Qt::AscendingOrder);
    }
}

bool PartsBrowser::eventFilter(QObject* obj, QEvent* ev) {
    if (grid_ && obj == grid_->viewport()) {
        switch (ev->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            if (handleTouch(static_cast<QTouchEvent*>(ev))) return true;
            break;
        case QEvent::Wheel: {
            // Ctrl (⌘ on a Mac)+wheel resizes the pictures; 8 px a notch.
            auto* w = static_cast<QWheelEvent*>(ev);
            if (!(w->modifiers() & Qt::ControlModifier)) break;
            wheelSteps_ += w->angleDelta().y() / 120.0;
            const int notches = static_cast<int>(wheelSteps_);
            if (notches != 0) {
                wheelSteps_ -= notches;
                chooseIconSize(iconSize_ + notches * 8);
            }
            return true;
        }
        case QEvent::NativeGesture: {
            // A trackpad pinch.
            auto* g = static_cast<QNativeGestureEvent*>(ev);
            if (g->gestureType() != Qt::ZoomNativeGesture) break;
            chooseIconSize(static_cast<int>(std::lround(iconSize_ * (1.0 + g->value()))));
            return true;
        }
        default:
            break;
        }
    }
    return QDockWidget::eventFilter(obj, ev);
}

bool PartsBrowser::handleTouch(QTouchEvent* e) {
    if (!TouchMode::fromTouchScreen(e) || e->points().isEmpty()) return false;
    e->accept();
    // Two fingers: pinch the pictures bigger or smaller.
    if (e->points().size() >= 2 && e->type() != QEvent::TouchEnd && e->type() != QEvent::TouchCancel) {
        const qreal dist = QLineF(e->points().at(0).position(), e->points().at(1).position()).length();
        if (touch_ != TouchState::Pinch) {
            holdTimer_->stop();
            QScroller::scroller(grid_->viewport())->stop();
            if (touch_ == TouchState::Drag) emit touchDragCancelled();
            touch_ = TouchState::Pinch;
            pinchStartDist_ = dist;
            pinchStartSize_ = iconSize_;
        } else if (pinchStartDist_ > 1) {
            chooseIconSize(static_cast<int>(std::lround(pinchStartSize_ * dist / pinchStartDist_)));
        }
        return true;
    }
    if (touch_ == TouchState::Pinch) {
        // One finger left of the pinch: nothing until they all lift.
        if (e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) touch_ = TouchState::None;
        return true;
    }
    const QEventPoint& p = e->points().first();
    const QPointF pos = p.position();
    QScroller* scroller = QScroller::scroller(grid_->viewport());
    const auto ts = static_cast<qint64>(e->timestamp());
    switch (e->type()) {
    case QEvent::TouchBegin: {
        touch_ = TouchState::Undecided;
        touchStart_ = pos;
        QListWidgetItem* it = grid_->itemAt(pos.toPoint());
        touchKey_ = it ? it->data(kPartKeyRole).toString() : QString();
        scroller->stop();
        if (!touchKey_.isEmpty()) holdTimer_->start();
        return true;
    }
    case QEvent::TouchUpdate: {
        const QPointF d = pos - touchStart_;
        if (touch_ == TouchState::Undecided && d.manhattanLength() > 12) {
            holdTimer_->stop();
            // Sideways off a part: take the part along; otherwise scroll.
            if (!touchKey_.isEmpty() && std::abs(d.x()) > std::abs(d.y())) {
                touch_ = TouchState::Drag;
            } else {
                touch_ = TouchState::Scroll;
                scroller->handleInput(QScroller::InputPress, touchStart_, ts);
            }
        }
        if (touch_ == TouchState::Drag) emit touchDragMoved(touchKey_, p.globalPosition().toPoint());
        else if (touch_ == TouchState::Scroll) scroller->handleInput(QScroller::InputMove, pos, ts);
        return true;
    }
    case QEvent::TouchEnd:
        holdTimer_->stop();
        if (touch_ == TouchState::Drag) {
            emit touchDragDropped(touchKey_, p.globalPosition().toPoint());
        } else if (touch_ == TouchState::Scroll) {
            scroller->handleInput(QScroller::InputRelease, pos, ts);
        } else if (touch_ == TouchState::Undecided && !touchKey_.isEmpty()) {
            if (QListWidgetItem* it = grid_->itemAt(touchStart_.toPoint())) grid_->setCurrentItem(it);
            emit partTapped(touchKey_);
        }
        touch_ = TouchState::None;
        return true;
    default:  // TouchCancel
        holdTimer_->stop();
        if (touch_ == TouchState::Drag) emit touchDragCancelled();
        touch_ = TouchState::None;
        return true;
    }
}

}
