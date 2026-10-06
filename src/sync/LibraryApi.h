#pragma once

// The server's modules, catalog and notices, for the desktop's Module
// library (the web's ModuleLibraryPanel, SaveModuleDialog, Catalog page and
// notice banner): your modules and your clubs' (GET /api/modules), a
// module's contents as a shared document (snapshot), saving one as a new
// version with a "What changed" note, its picture, renaming and deleting;
// the public catalog's modules, parts and collections, and adding them to
// you or a club; and the warnings you haven't acknowledged yet.
//
// Every call answers through a callback, or `failed` with what the server
// said (ServerRefusal: a 403 says why). A destroyed LibraryApi never calls
// back. The token rides in the Authorization header only.

#include "Credit.h"
#include "ServerApi.h"
#include "ServerRefusal.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkReply;

namespace bld::sync {

// Who owns a module: you ("user") or a club ("org").
struct OwnerTag {
    QString kind;  // user / org; empty when the server didn't say
    QString id;
    QString name;
    QString slug;  // a club's slug; empty for a person
    bool isClub() const { return kind == QLatin1String("org"); }
};

struct ServerModule {
    QString id;
    QString title;
    QString role;  // owner / editor / viewer
    OwnerTag owner;
    int docVersion = 0;
    int latestVersion = 0;  // the newest saved version; 0: no history yet
    qint64 thumbnailAt = 0;  // when its picture was made (ms); 0: none yet
    QDateTime updatedAt;
    Credit credit; // who made it; Take back / Give back

    bool canEdit() const { return role == QLatin1String("owner") || role == QLatin1String("editor"); }
    bool canDelete() const { return role == QLatin1String("owner"); }
    // "v3": the saved version, else the document's.
    int shownVersion() const { return latestVersion > 0 ? latestVersion : docVersion; }
    // Where its picture is (the cache key changes with each new picture).
    QString thumbnailPath() const;
};

struct CatalogSettings {
    bool modules = false;  // the module catalog is on
    bool parts = false;    // the parts catalog is on
    bool layouts = false;  // public layouts are on (servers before them: off)
    bool venues = false;   // public venues are on (servers before them: off)
};

struct CatalogItem {
    QString id;
    QString kind;  // module / part / layout / venue
    QString title;
    QString description;
    QString by;  // who shared it
    int uses = 0;
    int version = 0;
    QString previewPath;  // the drawn picture, a path on the server
    QString coverPath;    // the card's picture: its owner's own, else the drawn one
    // A layout or venue: its size in studs (0: unknown), a layout's part count (-1: none given).
    int widthStuds = 0;
    int heightStuds = 0;
    int partCount = -1;

    // The picture to show: the cover when the server sends one.
    QString picturePath() const { return coverPath.isEmpty() ? previewPath : coverPath; }
};

struct CatalogCollection {
    QString id;
    QString title;
    QString description;
    QString by;
    bool featured = false;
    bool official = false;
    int itemCount = 0;
    int modules = 0;
    int parts = 0;
    QString coverPath;  // empty: no picture
};

struct CollectionAddResult {
    int added = 0;
    int skipped = 0;  // you (or the club) had them already
    int failed = 0;
};

// A warning sent to you (or a club you run), as the web's notice banner shows it.
struct Notice {
    QString id;
    QString scope;     // site / club
    QString severity;  // note / warning / final
    QString reason;
    QString link;      // a page on the server, or empty
    QDateTime createdAt;
    bool acknowledged = false;
    QString clubName;  // a club warning: the club it's from
    QString toClub;    // sent to a club you run: its name
};

class LibraryApi : public QObject {
    Q_OBJECT
public:
    using Fail = std::function<void(const ServerRefusal&)>;

    explicit LibraryApi(QObject* parent = nullptr);

    void setBase(const QUrl& base) { base_ = base; }
    QUrl base() const { return base_; }
    void setToken(const QString& token) { token_ = token; }
    QString token() const { return token_; }

    // A module's page on the website (the web's module editor).
    QUrl moduleWebUrl(const QString& id) const;
    // A server path (a picture) as a full address.
    QUrl url(const QString& pathAndQuery) const;

    void listModules(std::function<void(const QList<ServerModule>&)> done, Fail failed);
    // The module's contents: a shared document (Yjs v1 update).
    void moduleSnapshot(const QString& id, std::function<void(const QByteArray&)> done, Fail failed);
    // One saved version's contents (Update from library: was it changed here?).
    void moduleVersionSnapshot(const QString& id, int version, std::function<void(const QByteArray&)> done, Fail failed);
    // A new, empty module of yours (orgSlug empty) or a club's.
    void createModule(const QString& title, const QString& orgSlug,
                      std::function<void(const QString& id, const QString& title)> done, Fail failed);
    // Save its contents: each save is a new version, with an optional note.
    void saveModuleSnapshot(const QString& id, const QByteArray& snapshot, const QString& note,
                            std::function<void(int version)> done, Fail failed);
    void setModuleThumbnail(const QString& id, const QByteArray& png, std::function<void()> done, Fail failed);
    void renameModule(const QString& id, const QString& title, std::function<void()> done, Fail failed);
    void deleteModule(const QString& id, std::function<void()> done, Fail failed);
    // A club's module (`kindPath` "modules", or "layouts", "venues", "custom-parts")
    // back to its author: taken back by them (`give` false) or given back by
    // the club's admins and managers. The club keeps a copy: its id.
    void returnToAuthor(const QString& kindPath, const QString& id, bool give,
                        std::function<void(const QString& keptCopyId)> done, Fail failed);
    // Any picture on the server (a module's thumbnail, a catalog preview).
    void picture(const QString& pathAndQuery, std::function<void(const QByteArray&)> done, Fail failed);

    void catalogSettings(std::function<void(const CatalogSettings&)> done, Fail failed);
    // kind: module / part; `query` searches titles, descriptions and tags.
    void catalogItems(const QString& kind, const QString& query, std::function<void(const QList<CatalogItem>&)> done,
                      Fail failed);
    // A copy of the item for you (orgSlug empty) or a club: its new id.
    void addCatalogItem(const QString& itemId, const QString& orgSlug,
                        std::function<void(const QString& kind, const QString& id)> done, Fail failed);
    // A venue of yours (or a club's): its name and its .bld-venue file.
    void venueFile(const QString& id, std::function<void(const QString& name, const QByteArray& file)> done, Fail failed);
    // A catalog item's page on the website (a layout's viewer, a venue's plan).
    QUrl catalogItemWebUrl(const QString& id) const;
    void collections(std::function<void(const QList<CatalogCollection>&)> done, Fail failed);
    void collectionItems(const QString& id, std::function<void(const QList<CatalogItem>&)> done, Fail failed);
    void addCollection(const QString& id, const QString& orgSlug, std::function<void(const CollectionAddResult&)> done,
                       Fail failed);

    // Your clubs and your role in each (where you may save a module).
    void orgs(std::function<void(const QList<OrgEntry>&)> done, Fail failed);

    void notices(std::function<void(const QList<Notice>&)> done, Fail failed);
    void acknowledge(const QString& id, std::function<void()> done, Fail failed);

    // How many requests are still out (tests).
    int pending() const { return pending_; }

    static ServerModule moduleFromJson(const QJsonObject& o);
    static CatalogItem catalogItemFromJson(const QJsonObject& o);
    static Notice noticeFromJson(const QJsonObject& o);

private:
    // `ok` gets the reply when it is a 2xx; `failed` gets the refusal otherwise.
    void send(const QByteArray& verb, const QString& path, const QByteArray& body, const QByteArray& contentType,
              std::function<void(QNetworkReply*)> ok, Fail failed);
    void sendJson(const QByteArray& verb, const QString& path, const QJsonObject& body,
                  std::function<void(const QJsonObject&)> ok, Fail failed);

    QNetworkAccessManager net_;
    QUrl base_;
    QString token_;
    int pending_ = 0;
};

// The path segment for an id (percent-encoded).
QString idPath(const QString& id);

}  // namespace bld::sync
