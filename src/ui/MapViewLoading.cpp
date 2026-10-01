// The loading card over the map, like the web's: "Opening layout…", then
// "Loading part pictures… 132 of 480" while a layout's pictures are read
// from disk, then nothing, or "3 pictures couldn't load" with Retry.
//
// Pictures are read on the UI thread (QPixmap), so the card repaints and
// the app handles its other events (not the user's clicks and keys) every
// few milliseconds while they load.

#include "LoadingCard.h"
#include "MapView.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>

namespace bld::ui {

namespace {
int s_progressStepMs = 30;
}

void MapView::setPictureProgressStepMs(int ms) { s_progressStepMs = ms; }

void MapView::showOpening(const QString& detail) {
    if (!loadingCard_) return;
    loadingCard_->setPlace(LoadingCard::Place::Centre);
    loadingCard_->showBusy(tr("Opening layout…"), detail);
    loadingCard_->paintNow();
}

void MapView::hideOpening() {
    if (loadingCard_ && !loadingCard_->failureShown()) loadingCard_->finish();
}

bool MapView::preloadPictures(const core::Map& map) {
    // Every distinct part with a picture, in the order the layout uses them.
    QStringList keys;
    QSet<QString> seen;
    for (const auto& layer : map.layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& brick : static_cast<const core::LayerBrick&>(*layer).bricks) {
            const QString key = brick.partNumber.toLower();
            if (seen.contains(key)) continue;
            seen.insert(key);
            const auto meta = parts_.metadata(key);
            if (meta && !meta->gifFilePath.isEmpty()) keys << key;
        }
    }
    QStringList toRead;
    for (const QString& k : std::as_const(keys))
        if (!parts_.pixmapTried(k)) toRead << k;

    // A loadMap() that arrives while the card paints (a live update) reads
    // its pictures straight away; the outer one then gives way to it.
    if (!toRead.isEmpty() && !preloading_ && loadingCard_) {
        preloading_ = true;
        const int generation = loadGeneration_;
        QElapsedTimer clock;
        clock.start();
        qint64 lastPaint = 0;
        for (int i = 0; i < toRead.size(); ++i) {
            parts_.pixmap(toRead[i]);
            if (clock.elapsed() - lastPaint < s_progressStepMs || !loadingCard_) continue;
            loadingCard_->setPlace(LoadingCard::Place::Centre);
            loadingCard_->showProgress(tr("Loading part pictures…"), i + 1, static_cast<int>(toRead.size()));
            loadingCard_->paintNow();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            lastPaint = clock.elapsed();
        }
        preloading_ = false;
        if (generation != loadGeneration_) return false;
    }

    failedPictures_.clear();
    for (const QString& k : std::as_const(keys))
        if (parts_.pixmap(k).isNull()) failedPictures_ << k;  // tried already: no disk read
    return true;
}

void MapView::finishLoading() {
    if (!loadingCard_) return;
    // Once hidden, the same failures don't come back on every live update.
    const bool sayFailed = !failedPictures_.isEmpty()
                           && (failedPictures_ != shownFailures_ || loadingCard_->failureShown());
    if (sayFailed) {
        loadingCard_->setPlace(LoadingCard::Place::Top);
        loadingCard_->showFailed(
            tr("%1 couldn't load").arg(LoadingCard::pictures(static_cast<int>(failedPictures_.size()))));
        loadingCard_->setToolTip(failedPictures_.join(QLatin1Char('\n')));
    } else {
        loadingCard_->finish();
    }
    shownFailures_ = failedPictures_;
}

void MapView::retryFailedPictures() {
    if (!map_) return;
    for (const QString& k : std::as_const(failedPictures_)) parts_.forgetPixmap(k);
    shownFailures_.clear();
    if (!preloadPictures(*map_)) return;
    rebuildScene();
    finishLoading();
}

void MapView::reloadPictures() {
    if (!map_) {
        rebuildScene();
        return;
    }
    if (!preloadPictures(*map_)) return;
    rebuildScene();
    finishLoading();
}

}  // namespace bld::ui
