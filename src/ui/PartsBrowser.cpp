#include "PartsBrowser.h"
#include "LoadingCard.h"
#include "BudgetSession.h"

#include "../core/Map.h"

#include "../parts/PartsLibrary.h"

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
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
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

constexpr int kIconSize = 96;

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

    grid_ = new DraggablePartsList(host);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setIconSize(QSize(kIconSize, kIconSize));
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
    grid_->setGridSize(QSize(kIconSize + 32, kIconSize + 52));
    loading_ = new LoadingCard(host);
    col->addWidget(loading_);
    col->addWidget(grid_);
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
    connect(grid_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos){
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
    });

    rebuild();
}

QString PartsBrowser::categoryForPath(const QString& absPath) const {
    const QFileInfo f(absPath);
    const QString parent = f.dir().dirName();
    return parent.isEmpty() ? tr("Other") : parent;
}

namespace {

void setPartIcon(parts::PartsLibrary& lib, const QString& key, QListWidgetItem* item) {
    QPixmap pm = lib.pixmap(key);
    if (!pm.isNull()) {
        item->setIcon(QIcon(pm.scaled(kIconSize, kIconSize,
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
                              bool withIcon = true) {
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
    if (withIcon) setPartIcon(lib, key, item);

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
        if (auto* item = makePartItem(lib_, key, cat, /*withIcon=*/false)) {
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
        if (QListWidgetItem* item = iconItems_.take(key)) setPartIcon(lib_, key, item);
        ++iconsDone_;
    }
    if (iconQueue_.isEmpty()) {
        iconTimer_->stop();
        loading_->finish();
    } else {
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
    if (auto* item = makePartItem(lib_, key, cat)) {
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
    grid_->setGridSize(QSize(kIconSize + 32, kIconSize + (numbers ? 68 : 52)));  // room for the numbers line
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
        default:
            break;
        }
    }
    return QDockWidget::eventFilter(obj, ev);
}

bool PartsBrowser::handleTouch(QTouchEvent* e) {
    if (!TouchMode::fromTouchScreen(e) || e->points().isEmpty()) return false;
    e->accept();
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
        return true;
    }
    case QEvent::TouchUpdate: {
        const QPointF d = pos - touchStart_;
        if (touch_ == TouchState::Undecided && d.manhattanLength() > 12) {
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
        if (touch_ == TouchState::Drag) emit touchDragCancelled();
        touch_ = TouchState::None;
        return true;
    }
}

}
