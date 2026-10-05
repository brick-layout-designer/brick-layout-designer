#include "PrefsSync.h"

#include "ServerApi.h"
#include "theme/AppPrefs.h"

#include <QJsonDocument>
#include <QSettings>

namespace bld::ui {

using theme::AppPrefs;

PrefsSync::PrefsSync(theme::PrefsStore& store, QObject* parent) : QObject(parent), store_(store) {
    push_.setSingleShot(true);
    push_.setInterval(1500);
    connect(&push_, &QTimer::timeout, this, &PrefsSync::pushNow);
    connect(&store_, &theme::PrefsStore::changed, this, [this](bool local) {
        if (local && active()) push_.start();
    });
}

PrefsSync::~PrefsSync() = default;

QString PrefsSync::hostKey(const QUrl& server) {
    QString key = server.host().toLower();
    if (server.port() > 0) key += QLatin1Char('_') + QString::number(server.port());
    return QStringLiteral("servers/") + key;
}

QDateTime PrefsSync::syncedAt(const QUrl& server) {
    return QDateTime::fromString(QSettings().value(hostKey(server) + QStringLiteral("/prefs/syncedAt")).toString(),
                                 Qt::ISODateWithMs);
}

QJsonObject PrefsSync::syncedPrefs(const QUrl& server) {
    return QJsonDocument::fromJson(QSettings().value(hostKey(server) + QStringLiteral("/prefs/json")).toByteArray())
        .object();
}

QString PrefsSync::host() const {
    if (!active()) return {};
    return server_.port() > 0 ? QStringLiteral("%1:%2").arg(server_.host()).arg(server_.port()) : server_.host();
}

void PrefsSync::start(const QUrl& server, const QString& token) {
    stop();
    server_ = server;
    api_ = new sync::ServerApi(this);
    api_->setBase(server);
    api_->setToken(token);
    connect(api_, &sync::ServerApi::preferencesReady, this, &PrefsSync::onPulled);
    connect(api_, &sync::ServerApi::preferencesSaved, this, [this](const QJsonObject& prefs, const QDateTime& at) {
        knowsIconSize_ = knowsIconSize_ || prefs.contains(QLatin1String("partsIconSize"));
        remember(prefs, at);
        // Still what we have (no change since): keep the server's time.
        if (AppPrefs::fromJson(prefs, store_.prefs()).sameSettings(store_.prefs())) store_.confirm(at);
        emit synced();
    });
    connect(api_, &sync::ServerApi::requestFailed, this,
            [this](const QString& what, const QString& message, bool unauthorized) {
                if (what != QLatin1String("preferences")) return;
                pulling_ = false;
                emit failed(message, unauthorized);
            });
    pulling_ = true;
    api_->fetchPreferences();
}

void PrefsSync::stop() {
    push_.stop();
    pulling_ = false;
    knowsIconSize_ = false;
    server_ = QUrl();
    if (api_) {
        api_->disconnect(this);
        api_->deleteLater();
        api_ = nullptr;
    }
}

void PrefsSync::onPulled(const QJsonObject& prefs, const QDateTime& updatedAt) {
    pulling_ = false;
    knowsIconSize_ = prefs.contains(QLatin1String("partsIconSize"));
    remember(prefs, updatedAt);
    const AppPrefs& local = store_.prefs();
    if (!updatedAt.isValid()) {
        // The account has none yet: ours become its settings, if they were ever set.
        if (local.updatedAt.isValid()) pushNow();
        else emit synced();
        return;
    }
    if (!local.updatedAt.isValid() || updatedAt > local.updatedAt) {
        AppPrefs theirs = AppPrefs::fromJson(prefs, local);
        theirs.updatedAt = updatedAt;
        if (theirs.sameSettings(local)) store_.confirm(updatedAt);
        else store_.adopt(theirs);
        emit synced();
        return;
    }
    if (local.updatedAt > updatedAt) {
        pushNow();
        return;
    }
    emit synced();
}

void PrefsSync::pushNow() {
    push_.stop();
    if (!api_) return;
    api_->savePreferences(outgoing());
}

QJsonObject PrefsSync::outgoing() const {
    QJsonObject json = store_.prefs().toJson();
    if (!knowsIconSize_) json.remove(QLatin1String("partsIconSize"));
    return json;
}

void PrefsSync::remember(const QJsonObject& prefs, const QDateTime& updatedAt) {
    if (!active()) return;
    QSettings s;
    s.beginGroup(hostKey(server_) + QStringLiteral("/prefs"));
    s.setValue(QStringLiteral("json"), QJsonDocument(prefs).toJson(QJsonDocument::Compact));
    s.setValue(QStringLiteral("syncedAt"), updatedAt.isValid() ? updatedAt.toUTC().toString(Qt::ISODateWithMs) : QString());
}

}  // namespace bld::ui
