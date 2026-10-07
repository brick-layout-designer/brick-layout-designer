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

// Who owns it: newer servers tag it ({kind, id, name, slug}); older ones give a club's id only.
OwnerTag ownerOf(const QJsonObject& o) {
    OwnerTag t;
    const QJsonObject owner = o.value(QLatin1String("owner")).toObject();
    t.kind = owner.value(QLatin1String("kind")).toString();
    t.id = owner.value(QLatin1String("id")).toString();
    t.name = owner.value(QLatin1String("name")).toString();
    t.slug = owner.value(QLatin1String("slug")).toString();
    if (t.kind.isEmpty()) {
        const QString org = o.value(QLatin1String("ownerOrgId")).toString();
        t.kind = org.isEmpty() ? QStringLiteral("user") : QStringLiteral("org");
        t.id = org.isEmpty() ? o.value(QLatin1String("ownerUserId")).toString() : org;
    }
    return t;
}

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
    m.owner = ownerOf(o);
    return m;
}

ServerPart LibraryApi::partFromJson(const QJsonObject& o) {
    ServerPart p;
    p.id = o.value(QLatin1String("id")).toString();
    p.partNumber = o.value(QLatin1String("partNumber")).toString();
    p.displayName = o.value(QLatin1String("displayName")).toString();
    p.role = o.value(QLatin1String("role")).toString();
    p.owner = ownerOf(o);
    p.updatedAt = timeOf(o.value(QLatin1String("updatedAt")));
    p.credit = creditFromJson(o);
    p.hasSprite = !o.value(QLatin1String("spriteMime")).toString().isEmpty();
    return p;
}

QString ServerPart::spritePath() const {
    if (!hasSprite) return {};
    // The date keeps the cached picture until the part changes.
    return QStringLiteral("/api/custom-parts/%1/sprite?v=%2").arg(idPath(id)).arg(updatedAt.toMSecsSinceEpoch());
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

void LibraryApi::moduleVersionSnapshot(const QString& id, int version, std::function<void(const QByteArray&)> done,
                                       Fail failed) {
    send("GET", QStringLiteral("/api/modules/%1/versions/%2/snapshot").arg(idPath(id)).arg(version), {}, {},
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
        s.review = o.value(QLatin1String("review")).toString() != QLatin1String("none");
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

void LibraryApi::layouts(std::function<void(const QList<LayoutEntry>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/layouts"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<LayoutEntry> out;
        for (const auto& v : o.value(QLatin1String("layouts")).toArray()) out << ServerApi::layoutEntryFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::venues(std::function<void(const QList<VenueEntry>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/venues"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<VenueEntry> out;
        for (const auto& v : o.value(QLatin1String("venues")).toArray()) out << ServerApi::venueEntryFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::customParts(std::function<void(const QList<ServerPart>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/custom-parts"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<ServerPart> out;
        for (const auto& v : o.value(QLatin1String("parts")).toArray()) out << partFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::layoutFile(const QString& id, bool native, std::function<void(const QByteArray&)> done, Fail failed) {
    send("GET", QStringLiteral("/api/layouts/%1/%2").arg(idPath(id), native ? QStringLiteral("export.bld-layout") : QStringLiteral("export.bbm")),
         {}, {}, [done = std::move(done)](QNetworkReply* r) {
             if (done) done(r->readAll());
         },
         std::move(failed));
}

void LibraryApi::deleteLayout(const QString& id, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/layouts/%1").arg(idPath(id)), {}, {}, [done = std::move(done)](QNetworkReply*) {
        if (done) done();
    }, std::move(failed));
}

void LibraryApi::deleteVenue(const QString& id, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/venues/%1").arg(idPath(id)), {}, {}, [done = std::move(done)](QNetworkReply*) {
        if (done) done();
    }, std::move(failed));
}

void LibraryApi::createVenue(const QString& name, const QJsonObject& data, const QString& orgSlug,
                             std::function<void(const QString&)> done, Fail failed) {
    QJsonObject body{ { QStringLiteral("name"), name }, { QStringLiteral("data"), data } };
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/venues"), body, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(o.value(QLatin1String("id")).toString());
    }, std::move(failed));
}

void LibraryApi::shareToCatalog(const CatalogShare& share, std::function<void(const QString&, const QString&)> done,
                                Fail failed) {
    QJsonObject body{ { QStringLiteral("kind"), share.kind }, { QStringLiteral("sourceId"), share.sourceId },
                      { QStringLiteral("title"), share.title.trimmed() } };
    // An update keeps its description and tags unless new ones are given (as the web sends it).
    if (!share.description.trimmed().isEmpty() || !share.update)
        body.insert(QStringLiteral("description"), share.description.trimmed());
    if (!share.tags.isEmpty() || !share.update) body.insert(QStringLiteral("tags"), QJsonArray::fromStringList(share.tags));
    if (!share.note.trimmed().isEmpty()) body.insert(QStringLiteral("note"), share.note.trimmed());
    if (!share.thumbnailPng.isEmpty())
        body.insert(QStringLiteral("thumbnail"),
                    QJsonObject{ { QStringLiteral("mime"), QStringLiteral("image/png") },
                                 { QStringLiteral("data"), QString::fromLatin1(share.thumbnailPng.toBase64()) } });
    sendJson("POST", QStringLiteral("/api/catalog/submissions"), body, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(o.value(QLatin1String("id")).toString(), o.value(QLatin1String("status")).toString());
    }, std::move(failed));
}

void LibraryApi::myCatalogItems(std::function<void(const QList<MyCatalogItem>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/mine"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<MyCatalogItem> out;
        for (const auto& v : o.value(QLatin1String("items")).toArray()) {
            const QJsonObject i = v.toObject();
            MyCatalogItem m;
            m.id = i.value(QLatin1String("id")).toString();
            m.kind = i.value(QLatin1String("kind")).toString();
            m.sourceId = i.value(QLatin1String("sourceId")).toString();
            m.title = i.value(QLatin1String("title")).toString();
            m.status = i.value(QLatin1String("status")).toString();
            m.reason = i.value(QLatin1String("reason")).toString();
            m.pendingVersion = intOf(i.value(QLatin1String("pendingVersion")));
            out << m;
        }
        if (done) done(out);
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
                             e.value(QLatin1String("myRole")).toString(), e.value(QLatin1String("canAdd")).toBool(true) };
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
