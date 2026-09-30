#include "SyncSession.h"

#include "WebModel.h"

#include "core/Map.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>

namespace bld::sync {

SyncSession::SyncSession(QObject* parent)
    : QObject(parent), clientId_(QRandomGenerator::global()->bounded(1u, 0x7fffffffu)), peers_(clientId_),
      client_(doc_) {
    connect(&client_, &SyncClient::statusChanged, this, [this](SyncClient::Status s) {
        if (s == SyncClient::Status::Synced) {
            unsynced_ = 0;
            // Two clocks on: the server marks our old connection's presence
            // gone one clock later, and a tie keeps it gone.
            if (presence_) {
                ++presenceClock_;
                sendPresence();
            }
        }
        // Disconnected: the server sends everyone's presence again on reconnect.
        if ((s == SyncClient::Status::Offline || s == SyncClient::Status::Connecting)
            && !peers_.states().isEmpty()) {
            peers_.clear();
            emit peersChanged();
        }
        emit statusChanged(s);
    });
    // y-protocols: say again we're here every 15 s (others drop a presence
    // not renewed for 30 s), and drop others' we haven't heard from in 30 s.
    renew_ = new QTimer(this);
    renew_->setInterval(15000);
    connect(renew_, &QTimer::timeout, this, [this] {
        if (presence_ && client_.status() == SyncClient::Status::Synced) sendPresence();
        dropStalePeers();
    });
    renew_->start();
    connect(&client_, &SyncClient::awarenessReceived, this, [this](const QByteArray& update) {
        // y-protocols: told that we left (the server tidying up an old
        // connection of ours), say again that we're here, with a newer clock.
        if (const auto entries = awareness::decode(update); entries && presence_)
            for (const auto& e : *entries)
                if (e.clientId == clientId_ && !e.state && e.clock >= presenceClock_) {
                    presenceClock_ = e.clock;
                    sendPresence();
                }
        if (peers_.apply(update)) emit peersChanged();
    });
    connect(&client_, &SyncClient::remoteChange, this, &SyncSession::remoteArrived);
    connect(&client_, &SyncClient::closedForGood, this, &SyncSession::ended);
}

SyncSession::~SyncSession() = default;

void SyncSession::open(const QUrl& socketUrl, const QString& token, bool readOnly) {
    readOnly_ = readOnly;
    client_.open(socketUrl, token);
}

void SyncSession::close() {
    if (presence_) setPresence(std::nullopt);
    client_.close();
}

void SyncSession::setPresence(const std::optional<QJsonObject>& state) {
    presence_ = state;
    sendPresence();
}

void SyncSession::dropStalePeers() {
    if (peers_.dropOlderThan(QDateTime::currentMSecsSinceEpoch() - 30000)) emit peersChanged();
}

void SyncSession::sendPresence() {
    ++presenceClock_;
    client_.sendAwareness(awareness::encode({ { clientId_, presenceClock_, presence_ } }));
}

std::unique_ptr<core::Map> SyncSession::currentMap(QString* error) const {
    return mapFromDocJson(doc_.toJson(), error);
}

void SyncSession::localEdit(const core::Map& map) {
    if (readOnly_ || !loaded_) return;
    const QByteArray update = doc_.writeMap(map);
    if (update.isEmpty()) return;
    if (client_.status() != SyncClient::Status::Synced) ++unsynced_;
    client_.sendLocal(update);
}

bool SyncSession::undo() {
    if (readOnly_) return false;
    const QByteArray update = doc_.undo();
    if (update.isEmpty()) return false;
    client_.sendLocal(update);
    emit mapChanged();
    return true;
}

bool SyncSession::redo() {
    if (readOnly_) return false;
    const QByteArray update = doc_.redo();
    if (update.isEmpty()) return false;
    client_.sendLocal(update);
    emit mapChanged();
    return true;
}

void SyncSession::holdRemoteChanges(bool hold) {
    held_ = hold;
    if (!held_ && pending_) {
        pending_ = false;
        emit mapChanged();
    }
}

void SyncSession::remoteArrived() {
    loaded_ = true;
    if (held_) {
        pending_ = true;
        return;
    }
    emit mapChanged();
}

}  // namespace bld::sync
