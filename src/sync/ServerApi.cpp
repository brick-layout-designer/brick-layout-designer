#include "ServerApi.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace bld::sync {

QByteArray userAgent() {
    const QString version = QCoreApplication::applicationVersion();
    return QStringLiteral("BrickLayoutDesigner/%1 (desktop)")
        .arg(version.isEmpty() ? QStringLiteral("dev") : version)
        .toUtf8();
}


namespace {

bool isThisMachine(const QString& host) {
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) return true;
    const QHostAddress a(host);
    return !a.isNull() && a.isLoopback();
}

QJsonObject jsonOf(QNetworkReply* r) { return QJsonDocument::fromJson(r->readAll()).object(); }

}  // namespace

ServerApi::ServerApi(QObject* parent) : QObject(parent) {
    poll_.setSingleShot(true);
    connect(&poll_, &QTimer::timeout, this, &ServerApi::pollToken);
}

std::optional<QUrl> ServerApi::normalizeBase(const QString& input, QString* error) {
    QString text = input.trimmed();
    if (!text.contains(QLatin1String("://"))) text.prepend(QLatin1String("https://"));
    QUrl url(text, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || (scheme != QLatin1String("https") && scheme != QLatin1String("http"))) {
        if (error) *error = tr("Not a server address: %1").arg(input.trimmed());
        return std::nullopt;
    }
    if (scheme == QLatin1String("http") && !isThisMachine(url.host())) {
        if (error) *error = tr("Use https:// for a server on another machine (http:// only works for this computer).");
        return std::nullopt;
    }
    url.setPath(QString());
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QUrl ServerApi::layoutSocketUrl(const QString& layoutId) const {
    QUrl url = base_;
    url.setScheme(base_.scheme() == QLatin1String("https") ? QStringLiteral("wss") : QStringLiteral("ws"));
    url.setPath(QStringLiteral("/ws/layout/") + QString::fromLatin1(QUrl::toPercentEncoding(layoutId)));
    return url;
}

QNetworkReply* ServerApi::get(const QString& path) {
    QUrl url = base_;
    url.setPath(path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    return net_.get(req);
}

QNetworkReply* ServerApi::post(const QString& path, const QJsonObject& body) {
    QUrl url = base_;
    url.setPath(path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    return net_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QNetworkReply* ServerApi::put(const QString& path, const QJsonObject& body) {
    QUrl url = base_;
    url.setPath(path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    return net_.put(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void ServerApi::fetchPreferences() {
    QNetworkReply* r = get(QStringLiteral("/api/me/preferences"));
    connect(r, &QNetworkReply::finished, this, [this, r] { onPreferencesReply(r, false); });
}

void ServerApi::savePreferences(const QJsonObject& prefs) {
    QNetworkReply* r = put(QStringLiteral("/api/me/preferences"), QJsonObject{ { QStringLiteral("prefs"), prefs } });
    connect(r, &QNetworkReply::finished, this, [this, r] { onPreferencesReply(r, true); });
}

void ServerApi::onPreferencesReply(QNetworkReply* r, bool saved) {
    r->deleteLater();
    const auto o = okJson(r, QStringLiteral("preferences"));
    if (!o) return;
    const QJsonObject prefs = o->value(QLatin1String("prefs")).toObject();
    // null until the account saves anything.
    const QDateTime at = QDateTime::fromString(o->value(QLatin1String("updatedAt")).toString(), Qt::ISODateWithMs);
    if (saved) emit preferencesSaved(prefs, at);
    else emit preferencesReady(prefs, at);
}

void ServerApi::fetchVersion() {
    QNetworkReply* r = get(QStringLiteral("/api/version"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() != QNetworkReply::NoError && status == 0) {
            emit requestFailed(QStringLiteral("version"), r->errorString(), false);
            return;
        }
        if (status != 200) {
            emit requestFailed(QStringLiteral("version"), tr("The server answered %1").arg(status), status == 401 || status == 403);
            return;
        }
        const QJsonObject o = jsonOf(r);
        ServerInfo info;
        info.version = o.value(QLatin1String("version")).toString();
        info.schemaVersion = o.value(QLatin1String("schemaVersion")).toInt();
        for (const auto& p : o.value(QLatin1String("protocols")).toArray()) info.protocols << p.toString();
        emit versionReady(info);
    });
}

void ServerApi::fetchLayouts() {
    QNetworkReply* r = get(QStringLiteral("/api/layouts"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const auto o = okJson(r, QStringLiteral("layouts"));
        if (!o) return;
        QList<LayoutEntry> out;
        for (const auto& v : o->value(QLatin1String("layouts")).toArray()) {
            const QJsonObject l = v.toObject();
            LayoutEntry e;
            e.id = l.value(QLatin1String("id")).toString();
            e.title = l.value(QLatin1String("title")).toString();
            e.ownerOrgName = l.value(QLatin1String("ownerOrgName")).toString();
            e.role = l.value(QLatin1String("role")).toString();
            const QJsonValue updated = l.value(QLatin1String("updatedAt"));
            e.updatedAt = updated.isDouble() ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(updated.toDouble()))
                                             : QDateTime::fromString(updated.toString(), Qt::ISODateWithMs);
            out << e;
        }
        emit layoutsReady(out);
    });
}

std::optional<QJsonObject> ServerApi::okJson(QNetworkReply* r, const QString& what) {
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) {
        // 403 is a token without the scope this needs (or one revoked since).
        const bool unauthorized = status == 401 || status == 403;
        emit requestFailed(what, status == 0 ? r->errorString() : tr("The server answered %1").arg(status), unauthorized);
        return std::nullopt;
    }
    return jsonOf(r);
}

void ServerApi::fetchCurrentUser() {
    QNetworkReply* r = get(QStringLiteral("/api/tokens/current"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const auto o = okJson(r, QStringLiteral("user"));
        if (!o) return;
        const QJsonObject u = o->value(QLatin1String("user")).toObject();
        emit currentUserReady(u.value(QLatin1String("id")).toString(),
                              u.value(QLatin1String("displayName")).toString());
    });
}

void ServerApi::fetchOrgs() {
    QNetworkReply* r = get(QStringLiteral("/api/orgs"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const auto o = okJson(r, QStringLiteral("orgs"));
        if (!o) return;
        QList<OrgEntry> out;
        for (const auto& v : o->value(QLatin1String("orgs")).toArray()) {
            const QJsonObject e = v.toObject();
            out << OrgEntry{ e.value(QLatin1String("slug")).toString(),
                             e.value(QLatin1String("name")).toString(),
                             e.value(QLatin1String("myRole")).toString() };
        }
        emit orgsReady(out);
    });
}

void ServerApi::publishLayout(const QString& title, const QByteArray& bbm, const QByteArray& sidecarJson,
                              const QString& orgSlug) {
    QJsonObject body{ { QStringLiteral("title"), title }, { QStringLiteral("bbm"), QString::fromUtf8(bbm) } };
    if (!sidecarJson.isEmpty()) body.insert(QStringLiteral("sidecar"), QString::fromUtf8(sidecarJson));
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    QNetworkReply* r = post(QStringLiteral("/api/layouts"), body);
    connect(r, &QNetworkReply::finished, this, [this, r, title] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = jsonOf(r);
        if (status != 201 && status != 200) {
            const QString err = o.value(QLatin1String("error")).toString();
            emit requestFailed(
                QStringLiteral("publish"),
                err.isEmpty() ? (status == 0 ? r->errorString() : tr("The server answered %1").arg(status))
                              : err,
                status == 401 || status == 403);
            return;
        }
        emit published(o.value(QLatin1String("id")).toString(),
                       o.value(QLatin1String("title")).toString(title));
    });
}

void ServerApi::fetchVenues() {
    QNetworkReply* r = get(QStringLiteral("/api/venues"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const auto o = okJson(r, QStringLiteral("venues"));
        if (!o) return;
        QList<VenueEntry> out;
        for (const auto& v : o->value(QLatin1String("venues")).toArray()) {
            const QJsonObject e = v.toObject();
            out << VenueEntry{ e.value(QLatin1String("id")).toString(), e.value(QLatin1String("name")).toString(),
                               e.value(QLatin1String("ownerOrgId")).toString() };
        }
        emit venuesReady(out);
    });
}

void ServerApi::fetchVenue(const QString& id) {
    QNetworkReply* r = get(QStringLiteral("/api/venues/") + QString::fromLatin1(QUrl::toPercentEncoding(id)));
    connect(r, &QNetworkReply::finished, this, [this, r, id] {
        r->deleteLater();
        const auto o = okJson(r, QStringLiteral("venue"));
        if (!o) return;
        // The server keeps the venue as the web wrote it: a .bld-venue file
        // without its schema tag (saveload/VenueIO.cpp reads either way).
        QJsonObject file = o->value(QLatin1String("data")).toObject();
        file.insert(QStringLiteral("schema"), QStringLiteral("bld-venue/1"));
        emit venueReady(id, o->value(QLatin1String("name")).toString(), QJsonDocument(file).toJson(QJsonDocument::Indented));
    });
}

void ServerApi::startSignIn(const QString& clientName) {
    cancelSignIn();
    // Signing in again: a dead token must not ride along (the device routes refuse tokens).
    token_.clear();
    QNetworkReply* r = post(QStringLiteral("/api/auth/device/code"),
                            QJsonObject{ { QStringLiteral("client_name"), clientName } });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = jsonOf(r);
        if (status != 200 || o.value(QLatin1String("device_code")).toString().isEmpty()) {
            emit signInFailed(status == 0 ? r->errorString() : tr("The server answered %1").arg(status));
            return;
        }
        deviceCode_ = o.value(QLatin1String("device_code")).toString();
        intervalSeconds_ = std::max(1, o.value(QLatin1String("interval")).toInt(5));
        DeviceCode code;
        code.userCode = o.value(QLatin1String("user_code")).toString();
        code.verificationUri = QUrl(o.value(QLatin1String("verification_uri")).toString());
        code.verificationUriComplete = QUrl(o.value(QLatin1String("verification_uri_complete")).toString());
        code.expiresInSeconds = o.value(QLatin1String("expires_in")).toInt();
        signInDeadline_ = QDateTime::currentDateTimeUtc().addMSecs(static_cast<qint64>(code.expiresInSeconds * msPerSecond_));
        emit signInCode(code);
        poll_.start(static_cast<int>(intervalSeconds_ * msPerSecond_));
    });
}

void ServerApi::cancelSignIn() {
    poll_.stop();
    deviceCode_.clear();
}

void ServerApi::pollToken() {
    if (deviceCode_.isEmpty()) return;
    if (QDateTime::currentDateTimeUtc() > signInDeadline_) {
        deviceCode_.clear();
        emit signInFailed(QStringLiteral("expired_token"));
        return;
    }
    const QString device = deviceCode_;
    QNetworkReply* r = post(QStringLiteral("/api/auth/device/token"),
                            QJsonObject{ { QStringLiteral("device_code"), device },
                                         { QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code") } });
    connect(r, &QNetworkReply::finished, this, [this, r, device] {
        r->deleteLater();
        if (device != deviceCode_) return;  // cancelled meanwhile
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = jsonOf(r);
        const QString token = o.value(QLatin1String("access_token")).toString();
        if (status == 200 && !token.isEmpty()) {
            deviceCode_.clear();
            emit signedIn(token);
            return;
        }
        const QString err = o.value(QLatin1String("error")).toString();
        if (err == QLatin1String("authorization_pending") || err == QLatin1String("slow_down") || status == 0) {
            // RFC 8628 §3.5: slow_down adds 5 seconds; network blips retry too.
            if (err == QLatin1String("slow_down"))
                intervalSeconds_ = std::max(intervalSeconds_ + 5, o.value(QLatin1String("interval")).toInt(0));
            poll_.start(static_cast<int>(intervalSeconds_ * msPerSecond_));
            return;
        }
        deviceCode_.clear();
        emit signInFailed(err.isEmpty() ? tr("The server answered %1").arg(status) : err);
    });
}

}  // namespace bld::sync
