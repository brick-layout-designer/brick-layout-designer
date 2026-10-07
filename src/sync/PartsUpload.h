#pragma once

// Desktop parts to the server (sync phase P4b, "Parts catalog sync"): the
// user's own parts (the imported-parts folder) that the server doesn't
// know are listed, and the ones the user confirms are uploaded as custom
// parts, owned by them or an organisation. Nothing uploads unasked.

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace bld::core { class Map; }

namespace bld::sync {

struct LocalPart {
    QString key;         // the part number, as the file is named (3001.8, MY.1)
    QString displayName; // the English description, else the key
    QString xmlPath;
    QString spritePath; // .gif or .png beside the XML
};

// What goes to the server for one part: its XML and a sprite that agree on
// the part's size in studs. The XML's <PixelsPerStud> always describes the
// sprite sent with it, so the web, the server and any desktop that
// downloads the part draw it at the same size as here.
struct PartPayload {
    QByteArray xml;
    QByteArray sprite;
    QString mime; // image/png or image/gif
    int pxPerStud = 8;
};

// The server's cap on one part's XML + sprite (MAX_PART_BLOB_BYTES).
inline constexpr qint64 kMaxPartBytes = 4 * 1024 * 1024;

class PartsUpload : public QObject {
    Q_OBJECT
public:
    PartsUpload(QUrl server, QString token, QObject* parent = nullptr);

    // Parts in `dir` (and below) with a sprite beside them: the sprite the
    // parts library draws (the .png for a hi-res part, else the .gif).
    static QList<LocalPart> scanFolder(const QString& dir);

    // The XML and sprite to upload for `part`. A hi-res part sends its
    // .png; one too big for `maxBytes` is sent at a lower resolution, its
    // <PixelsPerStud> lowered to match. A .gif is 8 px a stud whatever the
    // XML says, so its XML is sent without <PixelsPerStud>.
    static PartPayload payloadFor(const LocalPart& part, qint64 maxBytes = kMaxPartBytes);

    // Which of `local` the server's catalog (GET /api/parts/catalog) lacks.
    void findMissing(const QList<LocalPart>& local);
    // The catalog's keys and part numbers, upper-cased, for catalogReady().
    void fetchCatalog();
    // Upload `parts` as custom parts, personal or in the org `orgSlug`.
    void upload(const QList<LocalPart>& parts, const QString& orgSlug);

signals:
    void missingReady(const QList<bld::sync::LocalPart>& missing);
    void catalogReady(const QSet<QString>& known);
    void uploaded(int count, const QStringList& failed);
    void failed(const QString& message, bool unauthorized);

private:
    void getCatalog(std::function<void(const QSet<QString>&)> done);
    void uploadNext();

    QNetworkAccessManager net_;
    QUrl server_;
    QString token_;
    QList<LocalPart> queue_;
    QString orgSlug_;
    int done_ = 0;
    QStringList failures_;
};

// The part numbers `map` uses, upper-cased: bricks, and set / group parts.
QSet<QString> partNumbersIn(const core::Map& map);

// The user's own parts that `map` uses (bricks, and set / group parts)
// which the server doesn't know and weren't offered yet: the ones to offer
// for upload after an edit. `serverKnown` and `alreadyAsked` hold
// upper-cased part numbers; parts under `bundledRoot` (the ones shipped
// with the app) are never offered.
QList<LocalPart> partsToOffer(const core::Map& map, const QSet<QString>& serverKnown,
                              const QList<LocalPart>& local, const QSet<QString>& alreadyAsked,
                              const QString& bundledRoot = {});

} // namespace bld::sync
