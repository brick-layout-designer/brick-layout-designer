#include "ServerList.h"

#include "TokenStore.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace bld::sync {

namespace {

QJsonObject toJson(const ServerEntry& e) {
    QJsonObject o{ { QStringLiteral("url"), e.url.toString() },
                   { QStringLiteral("name"), e.name },
                   { QStringLiteral("main"), e.main },
                   { QStringLiteral("lastUsed"), e.lastUsed.isValid() ? e.lastUsed.toUTC().toString(Qt::ISODateWithMs) : QString() },
                   { QStringLiteral("userName"), e.userName },
                   { QStringLiteral("version"), e.version } };
    if (e.features) o.insert(QStringLiteral("features"), QJsonArray::fromStringList(*e.features));
    QJsonArray recent;
    for (const auto& r : e.recent)
        recent.append(QJsonObject{ { QStringLiteral("id"), r.id },
                                   { QStringLiteral("title"), r.title },
                                   { QStringLiteral("readOnly"), r.readOnly },
                                   { QStringLiteral("openedAt"), r.openedAt.toUTC().toString(Qt::ISODateWithMs) } });
    o.insert(QStringLiteral("recent"), recent);
    return o;
}

std::optional<ServerEntry> fromJson(const QJsonObject& o) {
    const auto url = ServerApi::normalizeBase(o.value(QStringLiteral("url")).toString());
    if (!url) return std::nullopt;
    ServerEntry e;
    e.url = *url;
    e.name = o.value(QStringLiteral("name")).toString();
    e.main = o.value(QStringLiteral("main")).toBool();
    e.lastUsed = QDateTime::fromString(o.value(QStringLiteral("lastUsed")).toString(), Qt::ISODateWithMs);
    e.userName = o.value(QStringLiteral("userName")).toString();
    e.version = o.value(QStringLiteral("version")).toString();
    if (o.contains(QStringLiteral("features"))) {
        QStringList f;
        for (const auto& v : o.value(QStringLiteral("features")).toArray()) f << v.toString();
        e.features = f;
    }
    for (const auto& v : o.value(QStringLiteral("recent")).toArray()) {
        const QJsonObject r = v.toObject();
        if (r.value(QStringLiteral("id")).toString().isEmpty()) continue;
        e.recent << RecentLayout{ r.value(QStringLiteral("id")).toString(), r.value(QStringLiteral("title")).toString(),
                                  r.value(QStringLiteral("readOnly")).toBool(),
                                  QDateTime::fromString(r.value(QStringLiteral("openedAt")).toString(), Qt::ISODateWithMs) };
    }
    return e;
}

QString appData() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }

}  // namespace

QString ServerEntry::key() const { return TokenStore::keyFor(url); }

QString ServerEntry::address() const {
    QString a = url.host();
    if (url.port() > 0) a += QLatin1Char(':') + QString::number(url.port());
    if (url.scheme() == QLatin1String("http")) a.prepend(QStringLiteral("http://"));
    return a;
}

QString ServerEntry::label() const {
    QString n = name.trimmed();
    if (!n.isEmpty()) return n;
    return url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
}

QStringList ServerEntry::missing() const {
    // Never asked: nothing to say yet.
    if (version.isEmpty() && !features) return {};
    return missingFeatures(features);
}

QString ServerList::folderName(const QUrl& url) {
    QString name = url.host().toLower();
    if (url.port() > 0) name += QLatin1Char('_') + QString::number(url.port());
    return name.replace(QLatin1Char(':'), QLatin1Char('_'));
}

ServerList ServerList::load() {
    ServerList list;
    QSettings s;
    if (!s.contains(QLatin1String(kKey))) {
        migrateLegacy(list);
        return list;
    }
    const auto doc = QJsonDocument::fromJson(s.value(QLatin1String(kKey)).toByteArray());
    for (const auto& v : doc.array()) {
        auto e = fromJson(v.toObject());
        if (e && !list.find(e->url)) list.servers_ << *e;
    }
    // Exactly one Main while there are any.
    bool seenMain = false;
    for (auto& e : list.servers_) {
        if (e.main && seenMain) e.main = false;
        seenMain = seenMain || e.main;
    }
    if (!seenMain && !list.servers_.isEmpty()) list.servers_.first().main = true;
    return list;
}

void ServerList::migrateLegacy(ServerList& list) {
    QSettings s;
    const auto url = ServerApi::normalizeBase(s.value(QLatin1String(kLegacyAddressKey)).toString());
    if (url) {
        ServerEntry& e = list.add(*url);
        e.main = true;
        // Its token is already in the keychain under the same key; its
        // folders were named by host alone, which differs only with a port.
        const QString oldName = QString(url->host()).replace(QLatin1Char(':'), QLatin1Char('_'));
        const QString newName = folderName(*url);
        if (oldName != newName) {
            for (const char* sub : { "/live/", "/server-parts/" }) {
                const QString from = appData() + QLatin1String(sub) + oldName;
                const QString to = appData() + QLatin1String(sub) + newName;
                if (QDir(from).exists() && !QDir(to).exists()) QDir().rename(from, to);
            }
            // The parts library listed the old server-parts folder.
            const QString key = QStringLiteral("partsLibrary/userPaths");
            QStringList paths = s.value(key).toStringList();
            const QString from = appData() + QStringLiteral("/server-parts/") + oldName;
            for (QString& p : paths)
                if (QDir::cleanPath(p) == QDir::cleanPath(from)) p = appData() + QStringLiteral("/server-parts/") + newName;
            if (s.contains(key)) s.setValue(key, paths);
        }
    }
    list.save();
}

void ServerList::save() const {
    QJsonArray a;
    for (const auto& e : servers_) a.append(toJson(e));
    QSettings().setValue(QLatin1String(kKey), QJsonDocument(a).toJson(QJsonDocument::Compact));
}

const ServerEntry* ServerList::find(const QUrl& url) const {
    const QString key = TokenStore::keyFor(url);
    for (const auto& e : servers_)
        if (e.key() == key) return &e;
    return nullptr;
}

ServerEntry* ServerList::find(const QUrl& url) {
    return const_cast<ServerEntry*>(std::as_const(*this).find(url));
}

const ServerEntry* ServerList::mainServer() const {
    for (const auto& e : servers_)
        if (e.main) return &e;
    return servers_.isEmpty() ? nullptr : &servers_.first();
}

const ServerEntry* ServerList::lastUsed() const {
    const ServerEntry* best = nullptr;
    for (const auto& e : servers_)
        if (e.lastUsed.isValid() && (!best || e.lastUsed > best->lastUsed)) best = &e;
    return best ? best : mainServer();
}

ServerEntry& ServerList::add(const QUrl& url, const QString& name) {
    if (ServerEntry* e = find(url)) return *e;
    ServerEntry e;
    e.url = url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo);
    e.name = name.trimmed();
    e.main = servers_.isEmpty();
    servers_ << e;
    return servers_.last();
}

bool ServerList::rename(const QUrl& url, const QString& name) {
    ServerEntry* e = find(url);
    if (!e) return false;
    e->name = name.trimmed();
    return true;
}

bool ServerList::remove(const QUrl& url) {
    const QString key = TokenStore::keyFor(url);
    const auto at = std::find_if(servers_.begin(), servers_.end(), [&](const ServerEntry& e) { return e.key() == key; });
    if (at == servers_.end()) return false;
    const bool wasMain = at->main;
    servers_.erase(at);
    if (wasMain && !servers_.isEmpty()) servers_.first().main = true;
    return true;
}

void ServerList::setMain(const QUrl& url) {
    if (!find(url)) return;
    const QString key = TokenStore::keyFor(url);
    for (auto& e : servers_) e.main = e.key() == key;
}

void ServerList::touch(const QUrl& url) { add(url).lastUsed = QDateTime::currentDateTimeUtc(); }

void ServerList::addRecent(const QUrl& url, const RecentLayout& layout) {
    ServerEntry& e = add(url);
    e.recent.removeIf([&](const RecentLayout& r) { return r.id == layout.id; });
    RecentLayout r = layout;
    if (!r.openedAt.isValid()) r.openedAt = QDateTime::currentDateTimeUtc();
    e.recent.prepend(r);
    while (e.recent.size() > kMaxRecent) e.recent.removeLast();
}

void ServerList::remember(const QUrl& url, const ServerInfo& info) {
    ServerEntry& e = add(url);
    e.version = info.version;
    e.features = info.features;
}

void ServerList::rememberUser(const QUrl& url, const QString& userName) { add(url).userName = userName; }

QUrl ServerList::settingsAccount() const {
    const ServerEntry* m = mainServer();
    return m ? m->url : QUrl();
}

}  // namespace bld::sync
