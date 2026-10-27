#pragma once

// The collaborative server's REST side, for the desktop's "Connect to
// Server…" (sync phase P4): version check, device sign-in (RFC 8628, as
// the server's /api/auth/device routes implement it), the layout list, and
// each layout's live-sync socket address. The token it gets goes to
// SyncClient; tokens only ever travel in the Authorization header.

#include "Credit.h"
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <optional>

#include "Compat.h"

class QNetworkReply;

namespace bld::sync {

// "CollaborativeBrickLayoutDesigner/<version> (desktop)" (servers also accept
// the "BrickLayoutDesigner/" of older versions): the User-Agent of every request
// the desktop makes to a collaborative server, the live socket included, so
// a server's filters can tell the app apart.
QByteArray userAgent();

// Newest shared-document schema this build reads (the web's DOC_SCHEMA_VERSION).
constexpr int kSupportedSchemaVersion = kDocSchemaVersion;

struct ServerInfo {
    QString     version;
    int         schemaVersion = 0;
    QStringList protocols;
    // The oldest desktop the server accepts and the one it recommends
    // (empty from servers before these checks), and where to get it.
    QString desktopMinimum;
    QString desktopRecommended;
    QString downloadUrl;
    // The oldest document schema the server reads (0: it didn't say).
    int docMinReadable = 0;
    // What the server can do; nullopt from servers before the list.
    std::optional<QStringList> features;

    // The server speaks our sync protocol, writes a document we read, and
    // reads the documents we write.
    bool compatible() const {
        return protocols.contains(QStringLiteral("y-websocket/1")) && canReadDoc(schemaVersion > 0 ? std::optional<int>(schemaVersion) : std::nullopt) &&
               (docMinReadable == 0 || kDocSchemaVersion >= docMinReadable);
    }
    // Where this build (or `app`) stands with the server.
    Standing standing(const QString& app) const { return desktopStanding(app, desktopMinimum, desktopRecommended); }
    // The server has the feature; servers that list none are taken to have
    // everything, as before (so nothing that worked stops working).
    bool has(const QString& feature) const { return !features || features->contains(feature); }
    // The server has usage limits, and may refuse with limit_reached and a
    // sentence to show (ServerRefusal).
    bool hasLimits() const { return features && features->contains(QStringLiteral("limits")); }
    // What this desktop uses that the server lacks (or, listing none, may lack).
    QStringList missing() const { return missingFeatures(features); }
};

struct DeviceCode {
    QString userCode;          // shown to the user, e.g. BCDF-GHJK
    QUrl    verificationUri;   // where they enter it
    QUrl    verificationUriComplete;  // the same, code pre-filled
    int     expiresInSeconds = 0;
};

struct LayoutEntry {
    QString   id;
    QString   title;
    QString   ownerOrgName;  // empty for personal layouts
    QString   ownerOrgSlug;  // the club's slug; empty for personal layouts
    QString   role;          // owner / editor / viewer
    QDateTime updatedAt;
    Credit credit; // who made it; Take back / Give back
};

struct VenueEntry {
    QString id;
    QString name;
    QString ownerOrgId;  // empty for personal venues
    QString ownerOrgName;  // the club's name (servers before owner tags: empty)
    QString ownerOrgSlug;  // the club's slug (servers before owner tags: empty)
    Credit credit;         // who made it; Take back / Give back
    bool canManage = true; // may rename, move or delete it (servers before this: true)
    // When it was saved and its outline's size in studs (servers before
    // layoutDownload: invalid and 0; 0 also when it has no outline yet).
    QDateTime createdAt;
    int widthStuds = 0;
    int heightStuds = 0;
};

// What is being saved to a club: each kind needs its own permission there.
enum class AddKind { Layout, Module, Part, Venue };

struct OrgEntry {
    QString slug;
    QString name;
    QString role; // your role: admin / manager / member
    // May you start new layouts in it? Servers before club roles: may you
    // save new things to it at all (false when the club keeps adding to its
    // admins and managers and you're a member); before that: true.
    bool canAdd = true;
    // What your roles in the club let you do ("edit_modules", "add_parts",
    // …: the server's access/clubPermissions.ts). Unset on servers before
    // club roles, which only send canAdd.
    std::optional<QStringList> permissions = std::nullopt;
    // Where its logo is on the server ("/api/orgs/<slug>/logo?v=…", which
    // changes with each new picture); empty when it has none. nullopt from
    // servers before club logos, which leave it out.
    std::optional<QString> logoUrl = std::nullopt;
    // You hold the club's Admin role (servers before club roles: role is admin).
    bool admin = false;
    // Admins and managers look after the club's layouts, venues, modules
    // and parts; only admins change its settings.
    bool managesThings() const { return role == QLatin1String("admin") || role == QLatin1String("manager"); }
    bool isAdmin() const { return admin || role == QLatin1String("admin"); }
    // May you manage its members on the website (invite people, or manage
    // members and roles; servers before club roles: its admins and managers)?
    bool managesMembers() const;
    // May you save this kind of thing to the club (the web's clubCanAdd)?
    bool canAddKind(AddKind kind) const;
    // An OrgEntry from GET /api/orgs.
    static OrgEntry fromJson(const QJsonObject& o);
};

// The permission that adds a new thing of this kind to a club.
QString addPermissionFor(AddKind kind);
// The clubs that will take this kind of thing from you.
QList<OrgEntry> clubsThatTake(const QList<OrgEntry>& orgs, AddKind kind);
// "Olive Club isn't listed: your roles there can't add modules. Ask a club
// admin for the Add or edit modules permission." Empty when none is left out.
QString leftOutNote(const QList<OrgEntry>& orgs, AddKind kind);

class ServerApi : public QObject {
    Q_OBJECT
public:
    explicit ServerApi(QObject* parent = nullptr);

    // The server address the user typed: https:// is required, except for
    // this machine (http://localhost, 127.0.0.1, [::1]). No scheme means
    // https. nullopt (and *error) otherwise.
    static std::optional<QUrl> normalizeBase(const QString& input, QString* error = nullptr);
    // A link the server (or a release feed) sent, as one the browser may
    // open: https, or http for this machine only, as for server addresses.
    // An empty QUrl for anything else.
    static QUrl webLink(const QString& text);

    void setBase(const QUrl& base) { base_ = base; }
    QUrl base() const { return base_; }
    void setToken(const QString& token) { token_ = token; }

    // wss://host/ws/layout/<id> (ws:// for http:// bases).
    QUrl layoutSocketUrl(const QString& layoutId) const;

    void fetchVersion();
    void fetchLayouts();
    // Who the token belongs to (GET /api/tokens/current): id and display name.
    void fetchCurrentUser();
    // Publish to Server (token scopes layouts:read for the orgs,
    // layouts:create to publish): the user's organisations, and a new
    // layout from a .bbm and its sidecar, personal or in an org.
    void fetchOrgs();
    // Your role on one layout now (GET /api/layouts/:id): layoutRoleReady
    // with owner / editor / viewer, or an empty role when it isn't yours to
    // open any more (404).
    void fetchLayoutRole(const QString& layoutId);
    void publishLayout(const QString& title, const QByteArray& bbm, const QByteArray& sidecarJson,
                       const QString& orgSlug);
    // Delete a layout (its owner): deleted(), or requestFailed("delete", …)
    // with the server's reason.
    void deleteLayout(const QString& id);
    // A club's layout or venue (`kindPath` "layouts" / "venues") back to its
    // author: taken back by them, or given back (`give`) by the club's admins
    // and managers. The club keeps its own copy. returned(id), or
    // requestFailed("return", …) with the server's reason.
    void returnToAuthor(const QString& kindPath, const QString& id, bool give);

    // The account's app settings (GET / PUT /api/me/preferences; token
    // scope layouts:read or account:prefs to read, account:prefs to save).
    // Saving merges the keys given. Both answer with the settings and when
    // they were last changed (invalid when never).
    void fetchPreferences();
    void savePreferences(const QJsonObject& prefs);

    // Device sign-in: signInCode once the server issued a code, then polls
    // until signedIn(token) or signInFailed(reason).
    void startSignIn(const QString& clientName);
    void cancelSignIn();

    // A layout or venue as the server lists it (GET /api/layouts, /api/venues).
    static LayoutEntry layoutEntryFromJson(const QJsonObject& o);
    static VenueEntry venueEntryFromJson(const QJsonObject& o);

    // Seconds between polls are multiplied by this (tests use a small one).
    void setPollIntervalScale(double msPerSecond) { msPerSecond_ = msPerSecond; }

signals:
    void versionReady(const bld::sync::ServerInfo& info);
    void layoutsReady(const QList<bld::sync::LayoutEntry>& layouts);
    void currentUserReady(const QString& userId, const QString& displayName);
    void orgsReady(const QList<bld::sync::OrgEntry>& orgs);
    void layoutRoleReady(const QString& layoutId, const QString& role);
    void published(const QString& layoutId, const QString& title);
    void deleted(const QString& id);
    void returned(const QString& id);
    void preferencesReady(const QJsonObject& prefs, const QDateTime& updatedAt);
    void preferencesSaved(const QJsonObject& prefs, const QDateTime& updatedAt);
    void signInCode(const bld::sync::DeviceCode& code);
    void signedIn(const QString& token);
    // access_denied, expired_token, or a network / server error.
    void signInFailed(const QString& reason);
    // A request failed: `what` is "version", "layouts", "orgs", "delete",
    // "return", "publish" or "preferences"; unauthorized is
    // true for a missing, revoked or expired token (sign in again).
    void requestFailed(const QString& what, const QString& message, bool unauthorized);

private:
    QNetworkReply* get(const QString& path);
    QNetworkReply* post(const QString& path, const QJsonObject& body);
    QNetworkReply* put(const QString& path, const QJsonObject& body);
    QNetworkReply* del(const QString& path);
    void deleteAt(const QString& path, const QString& id);
    void onPreferencesReply(QNetworkReply* r, bool saved);
    void pollToken();
    // The reply's JSON when it is a 200; otherwise emits requestFailed(what).
    std::optional<QJsonObject> okJson(QNetworkReply* r, const QString& what);

    QNetworkAccessManager net_;
    QUrl base_;
    QString token_;
    QString deviceCode_;
    int intervalSeconds_ = 5;
    double msPerSecond_ = 1000.0;
    QTimer poll_;
    QDateTime signInDeadline_;
};

}  // namespace bld::sync
