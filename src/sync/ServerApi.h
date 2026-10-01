#pragma once

// The collaborative server's REST side, for the desktop's "Connect to
// Server…" (sync phase P4): version check, device sign-in (RFC 8628, as
// the server's /api/auth/device routes implement it), the layout list, and
// each layout's live-sync socket address. The token it gets goes to
// SyncClient; tokens only ever travel in the Authorization header.

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <optional>

class QNetworkReply;

namespace bld::sync {

// "BrickLayoutDesigner/<version> (desktop)": the User-Agent of every request
// the desktop makes to a collaborative server, the live socket included, so
// a server's filters can tell the app apart.
QByteArray userAgent();

// Newest shared-document schema this build reads (the web's DOC_SCHEMA_VERSION).
constexpr int kSupportedSchemaVersion = 1;

struct ServerInfo {
    QString     version;
    int         schemaVersion = 0;
    QStringList protocols;
    // The server speaks our sync protocol and a document schema we read.
    bool compatible() const {
        return schemaVersion <= kSupportedSchemaVersion && protocols.contains(QStringLiteral("y-websocket/1"));
    }
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
};

struct VenueEntry {
    QString id;
    QString name;
    QString ownerOrgId;  // empty for personal venues
    QString ownerOrgName;  // the club's name (servers before owner tags: empty)
    QString ownerOrgSlug;  // the club's slug (servers before owner tags: empty)
};

struct OrgEntry {
    QString slug;
    QString name;
    QString role; // admin / member
};

class ServerApi : public QObject {
    Q_OBJECT
public:
    explicit ServerApi(QObject* parent = nullptr);

    // The server address the user typed: https:// is required, except for
    // this machine (http://localhost, 127.0.0.1, [::1]). No scheme means
    // https. nullopt (and *error) otherwise.
    static std::optional<QUrl> normalizeBase(const QString& input, QString* error = nullptr);

    void setBase(const QUrl& base) { base_ = base; }
    QUrl base() const { return base_; }
    void setToken(const QString& token) { token_ = token; }

    // wss://host/ws/layout/<id> (ws:// for http:// bases).
    QUrl layoutSocketUrl(const QString& layoutId) const;

    void fetchVersion();
    void fetchLayouts();
    // The venue library (token scope venues:read): the list, then one
    // venue as a .bld-venue file's bytes, ready to save into the local
    // Venue Library folder.
    // Who the token belongs to (GET /api/tokens/current): id and display name.
    void fetchCurrentUser();
    void fetchVenues();
    // Publish to Server (token scopes layouts:read for the orgs,
    // layouts:create to publish): the user's organisations, and a new
    // layout from a .bbm and its sidecar, personal or in an org.
    void fetchOrgs();
    void publishLayout(const QString& title, const QByteArray& bbm, const QByteArray& sidecarJson,
                       const QString& orgSlug);
    void fetchVenue(const QString& id);

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

    // Seconds between polls are multiplied by this (tests use a small one).
    void setPollIntervalScale(double msPerSecond) { msPerSecond_ = msPerSecond; }

signals:
    void versionReady(const bld::sync::ServerInfo& info);
    void layoutsReady(const QList<bld::sync::LayoutEntry>& layouts);
    void venuesReady(const QList<bld::sync::VenueEntry>& venues);
    void currentUserReady(const QString& userId, const QString& displayName);
    void orgsReady(const QList<bld::sync::OrgEntry>& orgs);
    void published(const QString& layoutId, const QString& title);
    void venueReady(const QString& id, const QString& name, const QByteArray& venueFile);
    void preferencesReady(const QJsonObject& prefs, const QDateTime& updatedAt);
    void preferencesSaved(const QJsonObject& prefs, const QDateTime& updatedAt);
    void signInCode(const bld::sync::DeviceCode& code);
    void signedIn(const QString& token);
    // access_denied, expired_token, or a network / server error.
    void signInFailed(const QString& reason);
    // A request failed: `what` is "version", "layouts", "venues",
    // "venue" or "preferences"; unauthorized is
    // true for a missing, revoked or expired token (sign in again).
    void requestFailed(const QString& what, const QString& message, bool unauthorized);

private:
    QNetworkReply* get(const QString& path);
    QNetworkReply* post(const QString& path, const QJsonObject& body);
    QNetworkReply* put(const QString& path, const QJsonObject& body);
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
