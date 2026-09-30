#pragma once

// Server parts to the desktop (sync phase P4b, "Parts catalog sync"): the
// server's manifest (GET /api/parts/manifest) lists every library and
// custom part the user can see, each hashed; only what is missing or
// changed is downloaded, into a cache folder per server that the parts
// library then reads. Libraries unchanged since the last sync (same hash)
// are skipped outright; within a changed one, only files whose sha256
// differs are fetched, and files the server no longer has are removed.

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <deque>

class QNetworkReply;

namespace bld::sync {

struct PartsSyncResult {
    int downloaded = 0; // files written
    int removed = 0;    // files the server no longer has
    int unchanged = 0;  // libraries and custom parts already up to date
    QStringList failed; // what couldn't be fetched, with why
};

class PartsSync : public QObject {
    Q_OBJECT
public:
    // `cacheDir`: this server's parts folder (libraries in libs/<slug>/,
    // custom parts in custom/); its state.json remembers the hashes synced.
    PartsSync(QUrl server, QString token, const QString& cacheDir, QObject* parent = nullptr);

    void start();
    const PartsSyncResult& result() const { return result_; }

    // Whether a manifest path stays inside its folder (no "..", no absolute path).
    static bool safeRelativePath(const QString& path);

signals:
    void progress(int done, int total);
    void finished(const bld::sync::PartsSyncResult& result);
    // The manifest itself couldn't be read; unauthorized: sign in again.
    void failed(const QString& message, bool unauthorized);

private:
    struct Fetch {
        QUrl url;
        QString target;     // absolute file path
        QString sha256;     // expected, or empty
        QString what;       // for failure messages
        QString spriteBase; // custom sprite: target without extension (picked from the reply)
        QString library;    // the library it belongs to, or
        QString customId;   // the custom part
    };
    QSet<QString> failedLibraries_, failedCustom_;
    QNetworkReply* get(const QUrl& url);
    void onManifest(const QJsonObject& manifest);
    void onLibrary(const QString& slug, const QJsonObject& lib);
    void next();
    void done();
    void saveState();

    QNetworkAccessManager net_;
    QUrl server_;
    QString token_;
    QDir cache_;
    QHash<QString, QString> libHashes_, customHashes_; // synced before
    QHash<QString, QString> newLibHashes_, newCustomHashes_;
    int pendingLibraries_ = 0;
    std::deque<Fetch> queue_;
    int total_ = 0, done_ = 0;
    bool fetching_ = false;
    PartsSyncResult result_;
};

} // namespace bld::sync
