#include "PartsUpload.h"
#include "ServerRefusal.h"

#include "ServerApi.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QXmlStreamReader>

namespace bld::sync {

namespace {

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// <Description><en>…</en></Description>, as BlueBrick parts describe themselves.
QString englishDescription(const QByteArray& xml) {
    QXmlStreamReader r(xml);
    bool inDescription = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("Description")) inDescription = true;
        else if (r.isEndElement() && r.name() == QLatin1String("Description")) inDescription = false;
        else if (inDescription && r.isStartElement() && r.name() == QLatin1String("en"))
            return r.readElementText().trimmed();
    }
    return {};
}

// A path in one comparable spelling: '/' separators (Windows paths may
// arrive with '\\'), cleaned, absolute, and, for the part that exists on
// disk, the real (canonical) spelling, so a symlinked or 8.3-style root and
// the files found under it agree. Both sides of a comparison go through
// this same function: mixing QDir::absolutePath (which leaves a drive-less
// "/opt/..." alone on Windows) with QFileInfo::absoluteFilePath (which
// gives it the current drive, "C:/opt/...") is what hid bundled parts there.
QString comparablePath(const QString& path) {
    if (path.isEmpty()) return {};
    const QString slashed = QDir::fromNativeSeparators(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString abs = QDir::cleanPath(QFileInfo(slashed).absoluteFilePath());
    // Canonicalise the deepest part that exists and keep the rest as written.
    QString head = abs, tail;
    for (;;) {
        QString real = QFileInfo(head).canonicalFilePath();
        if (!real.isEmpty()) {
            if (tail.isEmpty()) return real;
            return real.endsWith(QLatin1Char('/')) ? real + tail : real + QLatin1Char('/') + tail;
        }
        const qsizetype slash = head.lastIndexOf(QLatin1Char('/'));
        if (slash < 0) break;
        // Stop at the root, "/" or "C:/".
        const bool atRoot = slash == 0 || (slash == 2 && head.at(1) == QLatin1Char(':'));
        const QString parent = atRoot ? head.left(slash + 1) : head.left(slash);
        if (parent == head) break;
        const QString name = head.mid(slash + 1);
        tail = tail.isEmpty() ? name : name + QLatin1Char('/') + tail;
        head = parent;
    }
    return abs;
}

// Whether `path` lies below the folder `root`. Case-insensitive: Windows and
// macOS file systems are, and the drive letter's case varies on Windows.
bool isPathUnder(const QString& path, const QString& root) {
    const QString r = comparablePath(root), p = comparablePath(path);
    if (r.isEmpty() || p.isEmpty()) return false;
    const QString prefix = r.endsWith(QLatin1Char('/')) ? r : r + QLatin1Char('/');
    return p.startsWith(prefix, Qt::CaseInsensitive);
}

} // namespace

PartsUpload::PartsUpload(QUrl server, QString token, QObject* parent)
    : QObject(parent), server_(std::move(server)), token_(std::move(token)) {
}

QList<LocalPart> PartsUpload::scanFolder(const QString& dir) {
    QList<LocalPart> out;
    QDirIterator it(dir, { QStringLiteral("*.xml") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo xml(it.next());
        const QString base = xml.absolutePath() + QLatin1Char('/') + xml.completeBaseName();
        QString sprite;
        for (const char* ext : { ".gif", ".png" })
            if (QFile::exists(base + QLatin1String(ext))) {
                sprite = base + QLatin1String(ext);
                break;
            }
        if (sprite.isEmpty()) continue;
        const QString description = englishDescription(readAll(xml.absoluteFilePath()));
        out << LocalPart{ xml.completeBaseName(),
                          description.isEmpty() ? xml.completeBaseName() : description,
                          xml.absoluteFilePath(), sprite };
    }
    std::sort(out.begin(), out.end(), [](const LocalPart& a, const LocalPart& b) { return a.key < b.key; });
    return out;
}

void PartsUpload::getCatalog(std::function<void(const QSet<QString>&)> done) {
    QUrl url = server_;
    url.setPath(QStringLiteral("/api/parts/catalog"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    QNetworkReply* r = net_.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, done = std::move(done)] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            emit failed(status == 0 ? r->errorString() : tr("The server answered %1").arg(status),
                        status == 401 || status == 403);
            return;
        }
        QSet<QString> known;
        for (const auto& v :
             QJsonDocument::fromJson(r->readAll()).object().value(QLatin1String("parts")).toArray()) {
            const QJsonObject p = v.toObject();
            known.insert(p.value(QLatin1String("key")).toString().toUpper());
            known.insert(p.value(QLatin1String("partNumber")).toString().toUpper());
        }
        done(known);
    });
}

void PartsUpload::findMissing(const QList<LocalPart>& local) {
    getCatalog([this, local](const QSet<QString>& known) {
        QList<LocalPart> missing;
        for (const auto& p : local)
            if (!known.contains(p.key.toUpper())) missing << p;
        emit missingReady(missing);
    });
}

void PartsUpload::fetchCatalog() {
    getCatalog([this](const QSet<QString>& known) { emit catalogReady(known); });
}

void PartsUpload::upload(const QList<LocalPart>& parts, const QString& orgSlug) {
    queue_ = parts;
    orgSlug_ = orgSlug;
    done_ = 0;
    failures_.clear();
    uploadNext();
}

void PartsUpload::uploadNext() {
    if (queue_.isEmpty()) {
        emit uploaded(done_, failures_);
        return;
    }
    const LocalPart p = queue_.takeFirst();
    const QByteArray xml = readAll(p.xmlPath), sprite = readAll(p.spritePath);
    QJsonObject body{ { QStringLiteral("partNumber"), p.key },
                      { QStringLiteral("displayName"), p.displayName },
                      { QStringLiteral("xmlBase64"), QString::fromLatin1(xml.toBase64()) },
                      { QStringLiteral("spriteBase64"), QString::fromLatin1(sprite.toBase64()) },
                      { QStringLiteral("spriteMime"),
                        p.spritePath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
                            ? QStringLiteral("image/png")
                            : QStringLiteral("image/gif") } };
    if (!orgSlug_.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug_);
    QUrl url = server_;
    url.setPath(QStringLiteral("/api/custom-parts"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    QNetworkReply* r = net_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, key = p.key] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 201 || status == 200) ++done_;
        else {
            const ServerRefusal refusal = readRefusal(status, r->readAll(), r->errorString());
            const QString err = refusal.code.isEmpty() && !isLimitRefusal(refusal) && status != 0
                                    ? tr("the server answered %1").arg(status)
                                    : describe(refusal);
            failures_ << tr("%1: %2").arg(key, err);
            // A limit on the number of parts or on space, a read-only account,
            // a rate limit or a refused sign-in: every other upload would be
            // refused the same way. (A file too big on its own, 413, is just
            // that file.)
            const bool stopAll = status == 401 || status == 403 || refusal.code == QLatin1String("rate_limited");
            if (stopAll) {
                queue_.clear();
                emit failed(refusal.code.isEmpty() ? tr("The server refused the upload") : describe(refusal),
                            needsSignIn(refusal));
            }
        }
        uploadNext();
    });
}

QSet<QString> partNumbersIn(const core::Map& map) {
    QSet<QString> used;
    for (const auto& layer : map.layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        const auto& bricks = static_cast<const core::LayerBrick&>(*layer);
        for (const auto& b : bricks.bricks)
            if (!b.partNumber.isEmpty()) used.insert(b.partNumber.toUpper());
        for (const auto& g : bricks.groups)
            if (!g.partNumber.isEmpty()) used.insert(g.partNumber.toUpper());
    }
    return used;
}

QList<LocalPart> partsToOffer(const core::Map& map, const QSet<QString>& serverKnown,
                              const QList<LocalPart>& local, const QSet<QString>& alreadyAsked,
                              const QString& bundledRoot) {
    const QSet<QString> used = partNumbersIn(map);
    QList<LocalPart> out;
    QSet<QString> offered;
    for (const auto& p : local) {
        const QString key = p.key.toUpper();
        if (!used.contains(key) || serverKnown.contains(key) || alreadyAsked.contains(key)
            || offered.contains(key))
            continue;
        if (!bundledRoot.isEmpty() && isPathUnder(p.xmlPath, bundledRoot)) continue;
        offered.insert(key);
        out << p;
    }
    return out;
}

} // namespace bld::sync
