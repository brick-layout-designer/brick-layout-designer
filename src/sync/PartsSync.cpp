#include "PartsSync.h"
#include "ServerRefusal.h"

#include "ServerApi.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace bld::sync {

namespace {

QString sha256Of(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

bool writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return f.commit();
}

// A part number as a file name: path separators and odd characters out.
QString safeFileName(QString n) {
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    n.replace(bad, QStringLiteral("_"));
    while (n.startsWith(QLatin1Char('.'))) n.remove(0, 1);
    return n.isEmpty() ? QStringLiteral("part") : n;
}

} // namespace

PartsSync::PartsSync(QUrl server, QString token, const QString& cacheDir, QObject* parent)
    : QObject(parent), server_(std::move(server)), token_(std::move(token)), cache_(cacheDir) {
    QFile f(cache_.filePath(QStringLiteral("state.json")));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject s = QJsonDocument::fromJson(f.readAll()).object();
        // (QJsonObject::asKeyValueRange needs Qt 6.8; CI builds with 6.7.)
        const QJsonObject libs = s.value(QLatin1String("libraries")).toObject();
        for (auto it = libs.begin(); it != libs.end(); ++it) libHashes_.insert(it.key(), it.value().toString());
        const QJsonObject custom = s.value(QLatin1String("customParts")).toObject();
        for (auto it = custom.begin(); it != custom.end(); ++it) customHashes_.insert(it.key(), it.value().toString());
    }
}

bool PartsSync::safeRelativePath(const QString& path) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))
        || path.contains(QLatin1Char(':')))
        return false;
    for (const auto& seg : path.split(QRegularExpression(QStringLiteral(R"([/\\])"))))
        if (seg.isEmpty() || seg == QLatin1String("..") || seg == QLatin1String(".")) return false;
    return true;
}

QNetworkReply* PartsSync::get(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    // The token goes to the server's API only: part files are public, and
    // the server refuses a token on routes that don't take one.
    if (!token_.isEmpty() && url.host() == server_.host() && url.port() == server_.port()
        && url.path().startsWith(QLatin1String("/api/")))
        req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    return net_.get(req);
}

void PartsSync::start() {
    QUrl url = server_;
    url.setPath(QStringLiteral("/api/parts/manifest"));
    QNetworkReply* r = get(url);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            const ServerRefusal refusal = readRefusal(status, r->readAll(), r->errorString());
            emit failed(failureText(refusal), needsSignIn(refusal));
            return;
        }
        onManifest(QJsonDocument::fromJson(r->readAll()).object());
    });
}

void PartsSync::onManifest(const QJsonObject& manifest) {
    // Custom parts: fetch the ones whose hash changed.
    for (const auto& v : manifest.value(QLatin1String("customParts")).toArray()) {
        const QJsonObject p = v.toObject();
        const QString id = p.value(QLatin1String("id")).toString(),
                      hash = p.value(QLatin1String("hash")).toString();
        const QString number = safeFileName(p.value(QLatin1String("partNumber")).toString());
        newCustomHashes_.insert(id, hash);
        const QString base = cache_.filePath(QStringLiteral("custom/") + number);
        if (customHashes_.value(id) == hash && QFile::exists(base + QStringLiteral(".xml"))) {
            ++result_.unchanged;
            continue;
        }
        const auto url = [&](const char* key) {
            return server_.resolved(QUrl(p.value(QLatin1String(key)).toString()));
        };
        queue_.push_back({ url("xmlUrl"), base + QStringLiteral(".xml"), {}, number, {}, {}, id });
        queue_.push_back({ url("spriteUrl"), {}, {}, number, base, {}, id });
    }
    // Libraries: skip the ones whose hash didn't change; list the others' files.
    for (const auto& v : manifest.value(QLatin1String("libraries")).toArray()) {
        const QJsonObject lib = v.toObject();
        const QString slug = lib.value(QLatin1String("slug")).toString(),
                      hash = lib.value(QLatin1String("hash")).toString();
        if (!safeRelativePath(slug)) continue;
        newLibHashes_.insert(slug, hash);
        if (libHashes_.value(slug) == hash
            && QDir(cache_.filePath(QStringLiteral("libs/") + slug)).exists()) {
            ++result_.unchanged;
            continue;
        }
        ++pendingLibraries_;
        QUrl url = server_;
        url.setPath(QStringLiteral("/api/parts/manifest/libraries/") + slug);
        QNetworkReply* r = get(url);
        connect(r, &QNetworkReply::finished, this, [this, r, slug] {
            r->deleteLater();
            --pendingLibraries_;
            if (r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200)
                onLibrary(slug, QJsonDocument::fromJson(r->readAll()).object());
            else {
                result_.failed << tr("parts library %1: %2").arg(slug, r->errorString());
                failedLibraries_.insert(slug);
            }
            next();
        });
    }
    total_ = static_cast<int>(queue_.size());
    next();
}

void PartsSync::onLibrary(const QString& slug, const QJsonObject& lib) {
    const QDir dir(cache_.filePath(QStringLiteral("libs/") + slug));
    const QString prefix = lib.value(QLatin1String("urlPrefix")).toString();
    QSet<QString> keep;
    for (const auto& v : lib.value(QLatin1String("files")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString path = f.value(QLatin1String("path")).toString(),
                      sha = f.value(QLatin1String("sha256")).toString();
        if (!safeRelativePath(path)) {
            result_.failed << tr("parts library %1: refused the path %2").arg(slug, path);
            failedLibraries_.insert(slug);
            continue;
        }
        const QString target = dir.filePath(path);
        keep.insert(QFileInfo(target).absoluteFilePath());
        if (sha256Of(target) == sha) continue;
        QUrl url = server_;
        url.setPath(prefix + path);
        queue_.push_back({ url, target, sha, path, {}, slug, {} });
        ++total_;
    }
    // Files the server no longer has.
    QDirIterator it(dir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = QFileInfo(it.next()).absoluteFilePath();
        if (!keep.contains(f) && QFile::remove(f)) ++result_.removed;
    }
}

void PartsSync::next() {
    if (fetching_) return;
    if (queue_.empty()) {
        if (pendingLibraries_ == 0) done();
        return;
    }
    fetching_ = true;
    const Fetch f = queue_.front();
    queue_.pop_front();
    QNetworkReply* r = get(f.url);
    connect(r, &QNetworkReply::finished, this, [this, r, f] {
        r->deleteLater();
        fetching_ = false;
        ++done_;
        const QByteArray data = r->readAll();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString target = f.target;
        if (!f.spriteBase.isEmpty()) {
            const QString type = r->header(QNetworkRequest::ContentTypeHeader).toString();
            target =
                f.spriteBase
                + (type.contains(QLatin1String("png")) ? QStringLiteral(".png") : QStringLiteral(".gif"));
        }
        const QString got =
            QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        QString error;
        if (status != 200) error = tr("%1: the server answered %2").arg(f.what).arg(status);
        else if (!f.sha256.isEmpty() && got != f.sha256)
            error = tr("%1: the download didn't match its hash").arg(f.what);
        else if (!writeFile(target, data)) error = tr("%1: could not write %2").arg(f.what, target);
        if (error.isEmpty()) ++result_.downloaded;
        else {
            result_.failed << error;
            if (!f.library.isEmpty()) failedLibraries_.insert(f.library);
            if (!f.customId.isEmpty()) failedCustom_.insert(f.customId);
        }
        emit progress(done_, total_);
        next();
    });
}

void PartsSync::done() {
    // A library or part that failed is left out of the state, so the next sync tries it again.
    for (const auto& slug : std::as_const(failedLibraries_)) newLibHashes_.remove(slug);
    for (const auto& id : std::as_const(failedCustom_)) newCustomHashes_.remove(id);
    saveState();
    emit finished(result_);
}

void PartsSync::saveState() {
    QJsonObject libs, custom;
    for (auto [k, v] : newLibHashes_.asKeyValueRange()) libs.insert(k, v);
    for (auto [k, v] : newCustomHashes_.asKeyValueRange()) custom.insert(k, v);
    writeFile(cache_.filePath(QStringLiteral("state.json")),
              QJsonDocument(QJsonObject{ { QStringLiteral("libraries"), libs },
                                         { QStringLiteral("customParts"), custom } })
                  .toJson());
}

} // namespace bld::sync
