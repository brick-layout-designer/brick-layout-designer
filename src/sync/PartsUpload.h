#pragma once

// Desktop parts to the server (sync phase P4b, "Parts catalog sync"): the
// user's own parts (the imported-parts folder) that the server doesn't
// know are listed, and the ones the user confirms are uploaded as custom
// parts, owned by them or an organisation. Nothing uploads unasked.

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace bld::sync {

struct LocalPart {
    QString key;         // the part number, as the file is named (3001.8, MY.1)
    QString displayName; // the English description, else the key
    QString xmlPath;
    QString spritePath; // .gif or .png beside the XML
};

class PartsUpload : public QObject {
    Q_OBJECT
public:
    PartsUpload(QUrl server, QString token, QObject* parent = nullptr);

    // Parts in `dir` (and below) with a sprite beside them.
    static QList<LocalPart> scanFolder(const QString& dir);

    // Which of `local` the server's catalog (GET /api/parts/catalog) lacks.
    void findMissing(const QList<LocalPart>& local);
    // Upload `parts` as custom parts, personal or in the org `orgSlug`.
    void upload(const QList<LocalPart>& parts, const QString& orgSlug);

signals:
    void missingReady(const QList<bld::sync::LocalPart>& missing);
    void uploaded(int count, const QStringList& failed);
    void failed(const QString& message, bool unauthorized);

private:
    void uploadNext();

    QNetworkAccessManager net_;
    QUrl server_;
    QString token_;
    QList<LocalPart> queue_;
    QString orgSlug_;
    int done_ = 0;
    QStringList failures_;
};

} // namespace bld::sync
