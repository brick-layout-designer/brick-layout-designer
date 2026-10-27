#include "SyncSession.h"

#include "Compat.h"

#include <QJsonObject>

#include "WebModel.h"

#include "core/Map.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRandomGenerator>
#include <QTimer>

#include <utility>

namespace bld::sync {

namespace {
constexpr char kDocCache[] = "layout.ydoc";
}  // namespace

SyncSession::SyncSession(QObject* parent)
    : QObject(parent), clientId_(QRandomGenerator::global()->bounded(1u, 0x7fffffffu)), peers_(clientId_),
      client_(doc_) {
    connect(&client_, &SyncClient::statusChanged, this, [this](SyncClient::Status s) {
        if (s == SyncClient::Status::Synced) {
            if (before_) checkNothingDropped();
            if (offline_) QTimer::singleShot(0, this, [this] {
                    if (offline_ && client_.status() == SyncClient::Status::Synced) emit offlineEditsReady();
                });
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
    // The shared layout goes to disk at most once a second.
    saveDocTimer_ = new QTimer(this);
    saveDocTimer_->setSingleShot(true);
    saveDocTimer_->setInterval(1000);
    connect(saveDocTimer_, &QTimer::timeout, this, &SyncSession::saveDoc);
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
    socketUrl_ = socketUrl;
    token_ = token;
    before_.reset();
    notTaken_.reset();
    // Nothing of the layout open before: kept, it would merge into this one
    // and go to its server.
    doc_.reset();
    loaded_ = false;
    pending_ = false;
    offline_.reset();
    peers_.clear();
    loadCache();
    client_.open(socketUrl, token);
}

void SyncSession::setReadOnly(bool readOnly) {
    if (readOnly == readOnly_) return;
    readOnly_ = readOnly;
    // Up to editor: the server takes our edits from now on.
    if (!readOnly) return;
    // Down to view only. The server dropped our edits since the change, so
    // the document we hold may have some it lacks: start again from its
    // copy (not from the cache, which has them too), and keep what we
    // showed until we know.
    if (loaded_ || offline_) {
        if (const auto shown = editorMap()) before_ = Before{ merge::snapshotOf(*shown), doc_.encodeState(), offline_.has_value() };
    }
    // Offline edits leave the session but stay on disk until the window
    // has them (checkNothingDropped): closed before that, the layout opens
    // with them again.
    offline_.reset();
    saveDocTimer_->stop();
    if (!cacheDir_.isEmpty()) QFile::remove(QDir(cacheDir_).filePath(QString::fromLatin1(kDocCache)));
    client_.close();
    doc_.reset();
    loaded_ = false;
    pending_ = false;
    client_.open(socketUrl_, token_);
}

void SyncSession::checkNothingDropped() {
    if (!before_) return;
    const Before before = std::move(*before_);
    before_.reset();
    // Ours put on top of the server's copy: anything that changes is something it lacks.
    SyncDoc mine;
    mine.applyUpdate(doc_.encodeState());
    const QJsonObject server = mine.toJson();
    mine.applyUpdate(before.state);
    if (!before.offline && mine.toJson() == server) return;
    notTaken_ = before.shown;
    // The window keeps them from here, as an unsaved copy.
    if (before.offline && !offline_) saveOffline();
    QTimer::singleShot(0, this, [this] {
        if (notTaken_) emit editsNotTaken();
    });
}

void SyncSession::close() {
    if (presence_) setPresence(std::nullopt);
    if (saveDocTimer_->isActive()) {
        saveDocTimer_->stop();
        saveDoc();
    }
    client_.close();
}

namespace {

bool writeAll(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

}  // namespace

void SyncSession::loadCache() {
    if (cacheDir_.isEmpty()) return;
    // doc.bin was written by versions that kept the layout open before in
    // the document, so it may hold two layouts merged: not read again.
    QFile::remove(QDir(cacheDir_).filePath(QStringLiteral("doc.bin")));
    QFile doc(QDir(cacheDir_).filePath(QString::fromLatin1(kDocCache)));
    // A layout seen before opens from here while the connection comes up.
    if (doc.open(QIODevice::ReadOnly) && doc_.applyUpdate(doc.readAll())) loaded_ = true;
    QFile offline(QDir(cacheDir_).filePath(QStringLiteral("offline.json")));
    if (offline.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(offline.readAll()).object();
        if (o.contains(QLatin1String("mine")))
            offline_ = OfflineEdits{ merge::snapshotFromJson(o.value(QLatin1String("base")).toObject()),
                                     merge::snapshotFromJson(o.value(QLatin1String("mine")).toObject()),
                                     o.value(QLatin1String("count")).toInt(1) };
    }
    if (loaded_ || offline_) QTimer::singleShot(0, this, [this] { emit mapChanged(); });
}

void SyncSession::saveDoc() {
    if (!cacheDir_.isEmpty() && loaded_) writeAll(QDir(cacheDir_).filePath(QString::fromLatin1(kDocCache)), doc_.encodeState());
}

void SyncSession::saveOffline() {
    if (cacheDir_.isEmpty()) return;
    const QString path = QDir(cacheDir_).filePath(QStringLiteral("offline.json"));
    if (!offline_) {
        QFile::remove(path);
        return;
    }
    writeAll(path, QJsonDocument(QJsonObject{ { QStringLiteral("base"), merge::toJson(offline_->base) },
                                              { QStringLiteral("mine"), merge::toJson(offline_->mine) },
                                              { QStringLiteral("count"), offline_->count } })
                       .toJson(QJsonDocument::Compact));
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

std::unique_ptr<core::Map> SyncSession::editorMap(QString* error) const {
    return offline_ ? merge::mapOf(offline_->mine, error) : currentMap(error);
}

void SyncSession::localEdit(const core::Map& map) {
    if (readOnly_ || !loaded_) return;
    // Out of step with the server (or with offline edits still to
    // resolve): an offline edit, kept apart from the shared document.
    if (offline_ || client_.status() != SyncClient::Status::Synced) {
        const auto current = currentMap();
        if (!offline_) {
            if (!current) return;
            offline_ = OfflineEdits{ merge::snapshotOf(*current), {}, 0 };
        }
        offline_->mine = merge::snapshotOf(map);
        ++offline_->count;
        saveOffline();
        return;
    }
    const QByteArray update = doc_.writeMap(map);
    if (update.isEmpty()) return;
    client_.sendLocal(update);
    saveDocTimer_->start();
}

void SyncSession::resolveOffline(const core::Map* result) {
    if (!offline_) return;
    offline_.reset();
    saveOffline();
    if (result) localEdit(*result);
    emit mapChanged();
}

bool SyncSession::undo() {
    if (readOnly_ || offline_) return false;
    const QByteArray update = doc_.undo();
    if (update.isEmpty()) return false;
    client_.sendLocal(update);
    emit mapChanged();
    return true;
}

bool SyncSession::redo() {
    if (readOnly_ || offline_) return false;
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
    // A document written by a version that this one can't read (newer, or
    // much older): stop rather than sync changes we don't understand.
    const QJsonValue schema =
        doc_.toJson().value(QLatin1String("meta")).toObject().value(QLatin1String("schemaVersion"));
    if (schema.isDouble() && !canReadDoc(schema.toInt())) {
        client_.close();
        emit ended(kUnreadableDocCode, QStringLiteral("unreadable_doc"));
        return;
    }
    loaded_ = true;
    saveDocTimer_->start();
    // The editor keeps showing the offline edits until they're resolved.
    if (offline_) return;
    if (held_) {
        pending_ = true;
        return;
    }
    emit mapChanged();
}

}  // namespace bld::sync
