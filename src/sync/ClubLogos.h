#pragma once

// Club logos (the web's club logos, 0037): each club's picture beside its
// name wherever the desktop shows your clubs (the Server window's Show
// filter, the Save to pickers, the Module library's club groups).
//
// Every club list the app fetches (GET /api/orgs, through ServerApi or
// LibraryApi) is handed here. A club with a logoUrl gets its picture from
// the server, signed in, and kept in the app's data folder, one folder per
// server (<AppData>/club-logos/<server folder>/), so it shows at once next
// time. The address changes with each new picture: a club list that names
// another one fetches it again and drops the old file, and one without a
// logo drops it, so a change on the web shows without a restart (the
// server's club hint refreshes the list). A server before club logos sends
// no logoUrl and nothing is asked; a logo the server won't send (404) is
// left out quietly, and the club keeps its plain look.

#include "ServerApi.h"

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

namespace bld::sync {

class LibraryApi;

class ClubLogos : public QObject {
    Q_OBJECT
public:
    // The app's one cache (every window shows the same pictures).
    static ClubLogos& shared();

    explicit ClubLogos(QObject* parent = nullptr);

    // Where the pictures are kept; <AppData>/club-logos by default (tests use a temporary one).
    void setRoot(const QString& dir) { root_ = dir; }
    QString root() const;
    // This server's folder under root().
    QString folder(const QUrl& server) const;

    // A club list from the server, fetched with `token`: fetches the logos
    // it names that aren't kept yet and forgets the ones it no longer names.
    void update(const QUrl& server, const QString& token, const QList<OrgEntry>& orgs);

    // The club's logo; a null image when it has none (or it hasn't come yet).
    QImage logo(const QUrl& server, const QString& slug) const;
    QIcon icon(const QUrl& server, const QString& slug) const;
    // The server sends its clubs' logos (it names a logoUrl, even an empty one).
    bool offered(const QUrl& server) const;

    // Forget everything held in memory (tests; the files stay).
    void forget();

signals:
    // A logo of this server's came, changed or went.
    void changed(const QUrl& server);

private:
    struct Server {
        QHash<QString, QString> urls;    // slug → logoUrl
        QHash<QString, QImage> images;   // slug → its picture
        QSet<QString> fetching;          // logoUrls on their way
        QSet<QString> missing;           // logoUrls the server wouldn't send
        bool offered = false;
        LibraryApi* api = nullptr;
    };
    void store(Server& s, const QString& slug, const QImage& img);
    QString fileFor(const QUrl& server, const QString& logoUrl) const;

    QString root_;
    QHash<QString, Server> servers_;  // by ServerList::folderName
};

}  // namespace bld::sync
