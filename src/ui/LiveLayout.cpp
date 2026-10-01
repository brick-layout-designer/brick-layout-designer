#include "LiveLayout.h"
#include "ServerRefusal.h"

#include "MapView.h"
#include "MapViewInternal.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

#include <QDateTime>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QUndoStack>

namespace bld::ui {

LiveLayout::LiveLayout(MapView& view, QObject* parent) : QObject(parent), view_(view) {
    connect(&session_, &sync::SyncSession::mapChanged, this, &LiveLayout::reload);
    connect(&session_, &sync::SyncSession::statusChanged, this,
            [this] { emit statusTextChanged(statusText()); });
    connect(&session_, &sync::SyncSession::offlineEditsReady, this, [this] {
        emit statusTextChanged(statusText());
        emit offlineEditsReady();
    });
    connect(&session_, &sync::SyncSession::ended, this, [this](int code, const QString& reason) {
        close();
        const QString friendly = sync::liveCloseText(code, reason);
        emit ended(friendly.isEmpty() ? reason : friendly);
    });
    connect(view_.undoStack(), &QUndoStack::indexChanged, this, [this](int) {
        onLocalStep();
        drawPeers();
    });
    // Presence: our cursor and selection out (at most every 50 ms, like the
    // web), everyone else's drawn on the map.
    presenceTimer_ = new QTimer(this);
    presenceTimer_->setSingleShot(true);
    presenceTimer_->setInterval(50);
    connect(presenceTimer_, &QTimer::timeout, this, &LiveLayout::publishPresence);
    connect(&view_, &MapView::selectionChanged, this, &LiveLayout::schedulePresence);
    connect(&session_, &sync::SyncSession::peersChanged, this, &LiveLayout::drawPeers);
}

void LiveLayout::setUser(const QString& userId, const QString& displayName, const QString& layoutId) {
    user_ = { userId, displayName.isEmpty() ? QStringLiteral("Desktop") : displayName,
              sync::presence::colorFor(userId, layoutId) };
    schedulePresence();
}

void LiveLayout::schedulePresence() {
    if (active_ && !presenceTimer_->isActive()) presenceTimer_->start();
}

void LiveLayout::publishPresence() {
    if (!active_) return;
    QStringList bricks;
    for (QGraphicsItem* it : view_.scene()->selectedItems())
        if (detail::isBrickItem(it)) bricks << it->data(detail::kBrickDataGuid).toString();
    session_.setPresence(
        sync::presence::state(user_, cursorStuds_, bricks, QDateTime::currentMSecsSinceEpoch()));
}

void LiveLayout::drawPeers() {
    for (QGraphicsItem* it : std::as_const(peerItems_)) {
        view_.scene()->removeItem(it);
        delete it;
    }
    peerItems_.clear();
    drawnPeers_ = 0;
    if (!active_ || !view_.currentMap()) return;
    // Bricks by id, for selection outlines.
    QHash<QString, QRectF> bricks;
    for (const auto& layer : view_.currentMap()->layers())
        if (layer->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
                bricks.insert(b.guid, b.displayArea);
    const double px = detail::studToPx();
    const auto add = [this](QGraphicsItem* it) {
        it->setZValue(1e9);
        it->setAcceptedMouseButtons(Qt::NoButton);
        view_.scene()->addItem(it);
        peerItems_ << it;
    };
    for (const auto& state : session_.peers()) {
        const auto peer = sync::presence::peerFrom(state);
        const QColor color(peer.color);
        for (const auto& id : peer.brickIds) {
            const auto r = bricks.constFind(id);
            if (r == bricks.constEnd()) continue;
            auto* outline =
                new QGraphicsRectItem(QRectF(r->topLeft() * px, r->size() * px).adjusted(-2, -2, 2, 2));
            QPen pen(color, 2, Qt::DashLine);
            pen.setCosmetic(true);
            outline->setPen(pen);
            add(outline);
        }
        if (!peer.cursor) continue;
        const QPointF cursor = peer.cursor.value_or(QPointF());
        // The web's pointer arrow and name pill, the same size at any zoom.
        auto* arrow = new QGraphicsPolygonItem(
            QPolygonF({ { 0, 0 }, { 0, 16 }, { 4, 12 }, { 9, 22 }, { 11, 21 }, { 7, 11 }, { 12, 11 } }));
        arrow->setBrush(color);
        arrow->setPen(QPen(Qt::black, 0.5));
        arrow->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        arrow->setPos(cursor * px);
        add(arrow);
        auto* name = new QGraphicsSimpleTextItem(sync::presence::label(peer.name), arrow);
        QFont f = name->font();
        f.setPixelSize(10);
        name->setFont(f);
        name->setBrush(Qt::white);
        name->setPos(20, 4);
        auto* pill = new QGraphicsRectItem(QRectF(14, 2, name->boundingRect().width() + 12, 16), arrow);
        pill->setBrush(color);
        pill->setPen(Qt::NoPen);
        name->setParentItem(pill);
        name->setPos(20, 4);
        ++drawnPeers_;
    }
}

LiveLayout::~LiveLayout() {
    close();
}

void LiveLayout::open(const QUrl& socketUrl, const QString& token, bool readOnly, const QString& title,
                      const QString& cacheDir) {
    title_ = title;
    active_ = true;
    reloadedOnce_ = false;
    view_.viewport()->installEventFilter(this);
    view_.viewport()->setMouseTracking(true);
    session_.setCacheDir(cacheDir);
    session_.open(socketUrl, token, readOnly);
    schedulePresence();
    emit statusTextChanged(statusText());
}

void LiveLayout::close() {
    if (!active_) return;
    presenceTimer_->stop();
    active_ = false;
    drawPeers(); // clears them
    view_.viewport()->removeEventFilter(this);
    session_.holdRemoteChanges(false);
    session_.close();
    emit statusTextChanged(statusText());
}

bool LiveLayout::undo() {
    return active_ && session_.undo();
}
bool LiveLayout::redo() {
    return active_ && session_.redo();
}

QString LiveLayout::statusText() const {
    if (!active_) return {};
    QString s;
    switch (session_.status()) {
    case sync::SyncClient::Status::Synced:
        s = session_.unsyncedEdits() > 0
                ? tr("Connected, %n offline edit(s) to review", nullptr, session_.unsyncedEdits())
                : tr("Connected");
        break;
    case sync::SyncClient::Status::Offline:
        s = session_.unsyncedEdits() > 0
                ? tr("Offline, %n unsynced edit(s)", nullptr, session_.unsyncedEdits())
                : tr("Offline");
        break;
    default:
        s = !session_.loaded() ? tr("Connecting…")
            : session_.unsyncedEdits() > 0
                ? tr("Reconnecting…, %n unsynced edit(s)", nullptr, session_.unsyncedEdits())
                : tr("Reconnecting…");
    }
    return session_.readOnly() ? tr("%1, view only").arg(s) : s;
}

void LiveLayout::reload() {
    if (!active_) return;
    QString error;
    auto map = session_.editorMap(&error);
    if (!map) return;
    // Keep the view where it was, and the selection, across the reload.
    const bool first = !reloadedOnce_;
    const QTransform zoom = view_.transform();
    const int h = view_.horizontalScrollBar()->value(), v = view_.verticalScrollBar()->value();
    QSet<QString> selected;
    for (QGraphicsItem* it : view_.scene()->selectedItems())
        if (detail::isBrickItem(it)) selected.insert(it->data(detail::kBrickDataGuid).toString());

    reloading_ = true;
    view_.loadMap(std::move(map));
    reloading_ = false;
    reloadedOnce_ = true;

    if (!first) {
        view_.setTransform(zoom);
        view_.horizontalScrollBar()->setValue(h);
        view_.verticalScrollBar()->setValue(v);
    }
    if (!selected.isEmpty()) {
        for (QGraphicsItem* it : view_.scene()->items())
            if (detail::isBrickItem(it) && selected.contains(it->data(detail::kBrickDataGuid).toString()))
                it->setSelected(true);
    }
    drawPeers();
    emit mapReloaded();
    emit undoStateChanged();
    emit statusTextChanged(statusText());
}

void LiveLayout::onLocalStep() {
    if (!active_ || reloading_ || clearing_) return;
    auto* stack = view_.undoStack();
    if (stack->count() == 0) return;
    if (session_.readOnly()) {
        // View only: put the shared layout back.
        QTimer::singleShot(0, this, [this] { reload(); });
        return;
    }
    if (const core::Map* m = view_.currentMap()) {
        session_.localEdit(*m);
        emit localEdited();
    }
    // Undo goes through the shared document from here, so the local
    // stack doesn't keep this step (its commands would outlive the map
    // the next reload replaces).
    QTimer::singleShot(0, this, [this] {
        clearing_ = true;
        view_.undoStack()->clear();
        clearing_ = false;
        emit undoStateChanged();
        emit statusTextChanged(statusText());
    });
}

bool LiveLayout::eventFilter(QObject* watched, QEvent* event) {
    if (!active_ || watched != view_.viewport()) return QObject::eventFilter(watched, event);
    // View only: the left button (select, drag, place) does nothing;
    // panning and zooming still work.
    const auto leftButton = [event] { return static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton; };
    if (event->type() == QEvent::MouseMove) {
        cursorStuds_ =
            view_.mapToScene(static_cast<QMouseEvent*>(event)->position().toPoint()) / detail::studToPx();
        schedulePresence();
    } else if (event->type() == QEvent::Leave) {
        cursorStuds_.reset();
        schedulePresence();
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        if (session_.readOnly() && leftButton()) return true;
        session_.holdRemoteChanges(true);
        break;
    case QEvent::MouseButtonRelease:
        if (session_.readOnly() && leftButton()) return true;
        // After MapView has finished the drag (and pushed its command).
        QTimer::singleShot(0, this, [this] { session_.holdRemoteChanges(false); });
        break;
    case QEvent::Drop:
    case QEvent::DragEnter:
    case QEvent::DragMove:
        if (session_.readOnly()) return true;
        break;
    default: break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace bld::ui
