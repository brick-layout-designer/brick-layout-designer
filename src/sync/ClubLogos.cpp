#include "ClubLogos.h"

#include "LibraryApi.h"
#include "ServerList.h"
#include "ServerRefusal.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QStandardPaths>

namespace bld::sync {

namespace {
// The largest a logo is kept in memory (the server sends up to 512 px).
constexpr int kKeptSize = 128;
}  // namespace

ClubLogos& ClubLogos::shared() {
    // Parented to the application, so it goes before Qt does.
    static ClubLogos* one = new ClubLogos(QCoreApplication::instance());
    return *one;
}

ClubLogos::ClubLogos(QObject* parent) : QObject(parent) {}

QString ClubLogos::root() const {
    if (!root_.isEmpty()) return root_;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/club-logos");
}

QString ClubLogos::folder(const QUrl& server) const {
    const QString r = root();
    return r.isEmpty() ? QString() : r + QLatin1Char('/') + ServerList::folderName(server);
}

QString ClubLogos::fileFor(const QUrl& server, const QString& logoUrl) const {
    const QString dir = folder(server);
    if (dir.isEmpty()) return {};
    return dir + QLatin1Char('/') +
           QString::fromLatin1(QCryptographicHash::hash(logoUrl.toUtf8(), QCryptographicHash::Sha1).toHex()) +
           QStringLiteral(".img");
}

void ClubLogos::store(Server& s, const QString& slug, const QImage& img) {
    if (img.isNull()) {
        s.images.remove(slug);
        return;
    }
    const QImage kept = img.width() > kKeptSize || img.height() > kKeptSize
                            ? img.scaled(kKeptSize, kKeptSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                            : img;
    s.images.insert(slug, kept);
}

void ClubLogos::update(const QUrl& server, const QString& token, const QList<OrgEntry>& orgs) {
    if (!server.isValid()) return;
    const QString key = ServerList::folderName(server);
    Server& s = servers_[key];
    bool changedNow = false;

    // What this list names: only addresses on the server itself.
    QHash<QString, QString> urls;
    bool offered = false;
    for (const auto& o : orgs) {
        if (!o.logoUrl) continue;
        offered = true;
        if (o.logoUrl->startsWith(QLatin1Char('/')) && !o.logoUrl->startsWith(QStringLiteral("//")))
            urls.insert(o.slug, *o.logoUrl);
    }
    // No clubs says nothing about logos either way.
    if (offered || !orgs.isEmpty()) {
        if (s.offered != offered) changedNow = true;
        s.offered = offered;
    }

    // Ones that went or changed.
    for (auto it = s.urls.cbegin(); it != s.urls.cend(); ++it) {
        if (urls.value(it.key()) == it.value()) continue;
        if (s.images.contains(it.key())) changedNow = true;
        store(s, it.key(), {});
    }
    s.urls = urls;

    // The files kept: only the pictures this list names.
    const QString dir = folder(server);
    QSet<QString> keep;
    for (const QString& u : std::as_const(urls)) keep.insert(QFileInfo(fileFor(server, u)).fileName());
    if (!dir.isEmpty()) {
        const QStringList kept = QDir(dir).entryList({ QStringLiteral("*.img") }, QDir::Files);
        for (const QString& f : kept)
            if (!keep.contains(f)) QFile::remove(dir + QLatin1Char('/') + f);
    }

    if (!s.api) s.api = new LibraryApi(this);
    s.api->setBase(server);
    s.api->setToken(token);
    for (auto it = urls.cbegin(); it != urls.cend(); ++it) {
        const QString& slug = it.key();
        const QString& url = it.value();
        if (s.images.contains(slug) || s.fetching.contains(url) || s.missing.contains(url)) continue;
        const QString file = fileFor(server, url);
        if (!file.isEmpty() && QFile::exists(file)) {
            const QImage img(file);
            if (!img.isNull()) {
                store(s, slug, img);
                changedNow = true;
                continue;
            }
            QFile::remove(file);
        }
        s.fetching.insert(url);
        s.api->picture(url, [this, key, server, slug, url, file](const QByteArray& bytes) {
            Server& now = servers_[key];
            now.fetching.remove(url);
            QImage img;
            img.loadFromData(bytes);
            // A picture this build can't read: the plain look, not asked again.
            if (img.isNull()) {
                now.missing.insert(url);
                return;
            }
            if (now.urls.value(slug) != url) return;  // changed again meanwhile
            if (!file.isEmpty() && QDir().mkpath(QFileInfo(file).path())) {
                QFile f(file);
                if (f.open(QIODevice::WriteOnly)) f.write(bytes);
            }
            store(now, slug, img);
            emit changed(server);
        }, [this, key, url](const ServerRefusal& r) {
            Server& now = servers_[key];
            now.fetching.remove(url);
            // Not there, or not yours to see: the plain look, quietly. Offline:
            // asked again with the next club list.
            if (r.status == 403 || r.status == 404 || r.status == 410) now.missing.insert(url);
        });
    }
    if (changedNow) emit changed(server);
}

QImage ClubLogos::logo(const QUrl& server, const QString& slug) const {
    const auto it = servers_.constFind(ServerList::folderName(server));
    return it == servers_.cend() ? QImage() : it->images.value(slug);
}

QIcon ClubLogos::icon(const QUrl& server, const QString& slug) const {
    const QImage img = logo(server, slug);
    return img.isNull() ? QIcon() : QIcon(QPixmap::fromImage(img));
}

bool ClubLogos::offered(const QUrl& server) const {
    const auto it = servers_.constFind(ServerList::folderName(server));
    return it != servers_.cend() && it->offered;
}

void ClubLogos::forget() {
    for (auto& s : servers_) delete s.api;
    servers_.clear();
}

}  // namespace bld::sync
