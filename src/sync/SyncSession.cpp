#include "SyncSession.h"

#include "WebModel.h"

#include "core/Map.h"

namespace bld::sync {

SyncSession::SyncSession(QObject* parent) : QObject(parent), client_(doc_) {
    connect(&client_, &SyncClient::statusChanged, this, [this](SyncClient::Status s) {
        if (s == SyncClient::Status::Synced) unsynced_ = 0;
        emit statusChanged(s);
    });
    connect(&client_, &SyncClient::remoteChange, this, &SyncSession::remoteArrived);
    connect(&client_, &SyncClient::closedForGood, this, &SyncSession::ended);
}

SyncSession::~SyncSession() = default;

void SyncSession::open(const QUrl& socketUrl, const QString& token, bool readOnly) {
    readOnly_ = readOnly;
    client_.open(socketUrl, token);
}

void SyncSession::close() { client_.close(); }

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
