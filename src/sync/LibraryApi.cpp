#include "LibraryApi.h"
#include "ServerApi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace bld::sync {

QString idPath(const QString& id) { return QString::fromLatin1(QUrl::toPercentEncoding(id)); }

namespace {

QDateTime timeOf(const QJsonValue& v) {
    if (v.isDouble()) return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(v.toDouble()));
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

int intOf(const QJsonValue& v) { return v.isDouble() ? static_cast<int>(v.toDouble()) : 0; }

}  // namespace

QString ServerModule::thumbnailPath() const {
    if (thumbnailAt <= 0) return {};
    return QStringLiteral("/api/modules/%1/thumbnail?v=%2").arg(idPath(id)).arg(thumbnailAt);
}

LibraryApi::LibraryApi(QObject* parent) : QObject(parent) {}

QUrl LibraryApi::moduleWebUrl(const QString& id) const { return url(QStringLiteral("/modules/") + idPath(id)); }

QUrl LibraryApi::url(const QString& pathAndQuery) const {
    QUrl u = base_;
    const qsizetype q = pathAndQuery.indexOf(QLatin1Char('?'));
    u.setPath(q < 0 ? pathAndQuery : pathAndQuery.left(q), QUrl::TolerantMode);
    u.setQuery(q < 0 ? QString() : pathAndQuery.mid(q + 1));
    return u;
}

void LibraryApi::send(const QByteArray& verb, const QString& path, const QByteArray& body, const QByteArray& contentType,
                      std::function<void(QNetworkReply*)> ok, Fail failed) {
    QNetworkRequest req(url(path));
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    if (!contentType.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    QNetworkReply* r = verb == "GET" ? net_.get(req) : net_.sendCustomRequest(req, verb, body);
    ++pending_;
    // The reply belongs to net_, so it (and this connection) goes with us.
    connect(r, &QNetworkReply::finished, this, [this, r, ok = std::move(ok), failed = std::move(failed)] {
        r->deleteLater();
        --pending_;
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            if (ok) ok(r);
            return;
        }
        if (failed) failed(readRefusal(status, r->readAll(), r->errorString()));
    });
}

void LibraryApi::sendJson(const QByteArray& verb, const QString& path, const QJsonObject& body,
                          std::function<void(const QJsonObject&)> ok, Fail failed) {
    const bool get = verb == "GET";
    send(verb, path, get ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact),
         get ? QByteArray() : QByteArrayLiteral("application/json"),
         [ok = std::move(ok)](QNetworkReply* r) {
             if (ok) ok(QJsonDocument::fromJson(r->readAll()).object());
         },
         std::move(failed));
}

ServerModule LibraryApi::moduleFromJson(const QJsonObject& o) {
    ServerModule m;
    m.id = o.value(QLatin1String("id")).toString();
    m.title = o.value(QLatin1String("title")).toString();
    m.role = o.value(QLatin1String("role")).toString();
    m.docVersion = intOf(o.value(QLatin1String("docVersion")));
    m.latestVersion = intOf(o.value(QLatin1String("latestVersion")));
    const QJsonValue at = o.value(QLatin1String("thumbnailAt"));
    m.thumbnailAt = at.isDouble() ? static_cast<qint64>(at.toDouble()) : 0;
    m.updatedAt = timeOf(o.value(QLatin1String("updatedAt")));
    m.credit = creditFromJson(o);
    const QJsonObject owner = o.value(QLatin1String("owner")).toObject();
    m.owner.kind = owner.value(QLatin1String("kind")).toString();
    m.owner.id = owner.value(QLatin1String("id")).toString();
    m.owner.name = owner.value(QLatin1String("name")).toString();
    m.owner.slug = owner.value(QLatin1String("slug")).toString();
    if (m.owner.kind.isEmpty()) {
        // Servers before owner tags: a club's id only.
        const QString org = o.value(QLatin1String("ownerOrgId")).toString();
        m.owner.kind = org.isEmpty() ? QStringLiteral("user") : QStringLiteral("org");
        m.owner.id = org.isEmpty() ? o.value(QLatin1String("ownerUserId")).toString() : org;
    }
    return m;
}

CatalogItem LibraryApi::catalogItemFromJson(const QJsonObject& o) {
    CatalogItem it;
    it.id = o.value(QLatin1String("id")).toString();
    it.kind = o.value(QLatin1String("kind")).toString();
    it.title = o.value(QLatin1String("title")).toString();
    it.description = o.value(QLatin1String("description")).toString();
    it.by = o.value(QLatin1String("by")).toString();
    it.uses = intOf(o.value(QLatin1String("uses")));
    it.version = intOf(o.value(QLatin1String("version")));
    it.previewPath = o.value(QLatin1String("previewUrl")).toString();
    it.coverPath = o.value(QLatin1String("coverUrl")).toString();
    const QJsonObject sum = o.value(QLatin1String("summary")).toObject();
    it.widthStuds = intOf(sum.value(QLatin1String("widthStuds")));
    it.heightStuds = intOf(sum.value(QLatin1String("heightStuds")));
    if (sum.contains(QLatin1String("partCount"))) it.partCount = intOf(sum.value(QLatin1String("partCount")));
    return it;
}

Notice LibraryApi::noticeFromJson(const QJsonObject& o) {
    Notice n;
    n.id = o.value(QLatin1String("id")).toString();
    n.scope = o.value(QLatin1String("scope")).toString();
    n.severity = o.value(QLatin1String("severity")).toString();
    n.reason = o.value(QLatin1String("reason")).toString();
    n.link = o.value(QLatin1String("link")).toString();
    n.createdAt = timeOf(o.value(QLatin1String("createdAt")));
    const QJsonValue ack = o.value(QLatin1String("acknowledgedAt"));
    n.acknowledged = !ack.isNull() && !ack.isUndefined();
    n.clubName = o.value(QLatin1String("club")).toObject().value(QLatin1String("name")).toString();
    const QJsonObject to = o.value(QLatin1String("to")).toObject();
    if (to.value(QLatin1String("kind")).toString() == QLatin1String("org"))
        n.toClub = to.value(QLatin1String("name")).toString();
    return n;
}

void LibraryApi::listModules(std::function<void(const QList<ServerModule>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/modules"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<ServerModule> out;
        for (const auto& v : o.value(QLatin1String("modules")).toArray()) out << moduleFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::moduleSnapshot(const QString& id, std::function<void(const QByteArray&)> done, Fail failed) {
    send("GET", QStringLiteral("/api/modules/%1/snapshot").arg(idPath(id)), {}, {},
         [done = std::move(done)](QNetworkReply* r) {
             if (done) done(r->readAll());
         },
         std::move(failed));
}

void LibraryApi::createModule(const QString& title, const QString& orgSlug,
                              std::function<void(const QString&, const QString&)> done, Fail failed) {
    QJsonObject body{ { QStringLiteral("title"), title } };
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/modules"), body, [done = std::move(done), title](const QJsonObject& o) {
        if (done) done(o.value(QLatin1String("id")).toString(), o.value(QLatin1String("title")).toString(title));
    }, std::move(failed));
}

void LibraryApi::saveModuleSnapshot(const QString& id, const QByteArray& snapshot, const QString& note,
                                    std::function<void(int)> done, Fail failed) {
    QString path = QStringLiteral("/api/modules/%1/snapshot").arg(idPath(id));
    const QString trimmed = note.trimmed();
    if (!trimmed.isEmpty())
        path += QStringLiteral("?note=") + QString::fromLatin1(QUrl::toPercentEncoding(trimmed));
    send("PUT", path, snapshot, QByteArrayLiteral("application/octet-stream"),
         [done = std::move(done)](QNetworkReply* r) {
             const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
             if (done) done(intOf(o.value(QLatin1String("version"))));
         },
         std::move(failed));
}

void LibraryApi::setModuleThumbnail(const QString& id, const QByteArray& png, std::function<void()> done, Fail failed) {
    // Base64 in JSON: the site's firewall lets octet-stream through on the snapshot routes only.
    sendJson("PUT", QStringLiteral("/api/modules/%1/thumbnail").arg(idPath(id)),
             QJsonObject{ { QStringLiteral("mime"), QStringLiteral("image/png") },
                          { QStringLiteral("data"), QString::fromLatin1(png.toBase64()) } },
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

void LibraryApi::renameModule(const QString& id, const QString& title, std::function<void()> done, Fail failed) {
    sendJson("PATCH", QStringLiteral("/api/modules/%1").arg(idPath(id)), QJsonObject{ { QStringLiteral("title"), title } },
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

void LibraryApi::deleteModule(const QString& id, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/modules/%1").arg(idPath(id)), {}, {},
         [done = std::move(done)](QNetworkReply*) {
             if (done) done();
         },
         std::move(failed));
}

void LibraryApi::returnToAuthor(const QString& kindPath, const QString& id, bool give,
                                std::function<void(const QString&)> done, Fail failed) {
    sendJson(
        "POST",
        QStringLiteral("/api/%1/%2/%3")
            .arg(kindPath, idPath(id), give ? QStringLiteral("give-back") : QStringLiteral("take-back")),
        QJsonObject(),
        [done = std::move(done)](const QJsonObject& o) {
            if (done) done(o.value(QLatin1String("keptCopyId")).toString());
        },
        std::move(failed));
}

void LibraryApi::picture(const QString& pathAndQuery, std::function<void(const QByteArray&)> done, Fail failed) {
    send("GET", pathAndQuery, {}, {}, [done = std::move(done)](QNetworkReply* r) {
        if (done) done(r->readAll());
    }, std::move(failed));
}

void LibraryApi::catalogSettings(std::function<void(const CatalogSettings&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/settings"), {}, [done = std::move(done)](const QJsonObject& o) {
        CatalogSettings s;
        s.modules = o.value(QLatin1String("modules")).toBool();
        s.parts = o.value(QLatin1String("parts")).toBool();
        s.layouts = o.value(QLatin1String("layouts")).toBool();
        s.venues = o.value(QLatin1String("venues")).toBool();
        if (done) done(s);
    }, std::move(failed));
}

void LibraryApi::catalogItems(const QString& kind, const QString& query,
                              std::function<void(const QList<CatalogItem>&)> done, Fail failed) {
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("kind"), kind);
    if (!query.trimmed().isEmpty()) q.addQueryItem(QStringLiteral("q"), query.trimmed());
    q.addQueryItem(QStringLiteral("sort"), QStringLiteral("popular"));
    sendJson("GET", QStringLiteral("/api/catalog/items?") + q.toString(QUrl::FullyEncoded), {},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<CatalogItem> out;
                 for (const auto& v : o.value(QLatin1String("items")).toArray()) out << catalogItemFromJson(v.toObject());
                 if (done) done(out);
             },
             std::move(failed));
}

void LibraryApi::addCatalogItem(const QString& itemId, const QString& orgSlug,
                                std::function<void(const QString&, const QString&)> done, Fail failed) {
    QJsonObject body;
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/catalog/items/%1/add").arg(idPath(itemId)), body,
             [done = std::move(done)](const QJsonObject& o) {
                 if (done) done(o.value(QLatin1String("kind")).toString(), o.value(QLatin1String("id")).toString());
             },
             std::move(failed));
}

void LibraryApi::venueFile(const QString& id, std::function<void(const QString&, const QByteArray&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/venues/%1").arg(idPath(id)), {}, [done = std::move(done)](const QJsonObject& o) {
        // Kept as the web wrote it: a .bld-venue file without its schema tag.
        QJsonObject file = o.value(QLatin1String("data")).toObject();
        file.insert(QStringLiteral("schema"), QStringLiteral("bld-venue/1"));
        if (done) done(o.value(QLatin1String("name")).toString(), QJsonDocument(file).toJson(QJsonDocument::Indented));
    }, std::move(failed));
}

QUrl LibraryApi::catalogItemWebUrl(const QString& id) const { return url(QStringLiteral("/catalog/items/") + idPath(id)); }

void LibraryApi::collections(std::function<void(const QList<CatalogCollection>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/collections"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<CatalogCollection> out;
        for (const auto& v : o.value(QLatin1String("collections")).toArray()) {
            const QJsonObject c = v.toObject();
            CatalogCollection e;
            e.id = c.value(QLatin1String("id")).toString();
            e.title = c.value(QLatin1String("title")).toString();
            e.description = c.value(QLatin1String("description")).toString();
            e.by = c.value(QLatin1String("by")).toString();
            e.featured = c.value(QLatin1String("featured")).toBool();
            e.official = c.value(QLatin1String("official")).toBool();
            e.itemCount = intOf(c.value(QLatin1String("itemCount")));
            e.modules = intOf(c.value(QLatin1String("modules")));
            e.parts = intOf(c.value(QLatin1String("parts")));
            e.coverPath = c.value(QLatin1String("coverUrl")).toString();
            out << e;
        }
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::collectionItems(const QString& id, std::function<void(const QList<CatalogItem>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/collections/%1").arg(idPath(id)), {},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<CatalogItem> out;
                 for (const auto& v : o.value(QLatin1String("items")).toArray()) out << catalogItemFromJson(v.toObject());
                 if (done) done(out);
             },
             std::move(failed));
}

void LibraryApi::addCollection(const QString& id, const QString& orgSlug,
                               std::function<void(const CollectionAddResult&)> done, Fail failed) {
    QJsonObject body;
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/catalog/collections/%1/add").arg(idPath(id)), body,
             [done = std::move(done)](const QJsonObject& o) {
                 CollectionAddResult r;
                 r.added = static_cast<int>(o.value(QLatin1String("added")).toArray().size());
                 r.skipped = static_cast<int>(o.value(QLatin1String("skipped")).toArray().size());
                 r.failed = static_cast<int>(o.value(QLatin1String("failed")).toArray().size());
                 if (done) done(r);
             },
             std::move(failed));
}

void LibraryApi::orgs(std::function<void(const QList<OrgEntry>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/orgs"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<OrgEntry> out;
        for (const auto& v : o.value(QLatin1String("orgs")).toArray()) {
            const QJsonObject e = v.toObject();
            out << OrgEntry{ e.value(QLatin1String("slug")).toString(), e.value(QLatin1String("name")).toString(),
                             e.value(QLatin1String("myRole")).toString() };
        }
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::notices(std::function<void(const QList<Notice>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/notices"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<Notice> out;
        for (const auto& v : o.value(QLatin1String("notices")).toArray()) out << noticeFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::acknowledge(const QString& id, std::function<void()> done, Fail failed) {
    sendJson("POST", QStringLiteral("/api/notices/%1/acknowledge").arg(idPath(id)), {},
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

}  // namespace bld::sync
