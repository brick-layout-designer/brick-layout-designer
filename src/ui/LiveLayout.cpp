#include "LiveLayout.h"

#include "MapView.h"
#include "MapViewInternal.h"

#include "core/Map.h"

#include <QEvent>
#include <QGraphicsItem>
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
    connect(&session_, &sync::SyncSession::ended, this, [this](int, const QString& reason) {
        close();
        emit ended(reason);
    });
    connect(view_.undoStack(), &QUndoStack::indexChanged, this, [this](int) { onLocalStep(); });
}

LiveLayout::~LiveLayout() {
    close();
}

void LiveLayout::open(const QUrl& socketUrl, const QString& token, bool readOnly, const QString& title) {
    title_ = title;
    active_ = true;
    reloadedOnce_ = false;
    view_.viewport()->installEventFilter(this);
    session_.open(socketUrl, token, readOnly);
    emit statusTextChanged(statusText());
}

void LiveLayout::close() {
    if (!active_) return;
    active_ = false;
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
    case sync::SyncClient::Status::Synced: s = tr("Connected"); break;
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
    auto map = session_.currentMap(&error);
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
    if (const core::Map* m = view_.currentMap()) session_.localEdit(*m);
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
