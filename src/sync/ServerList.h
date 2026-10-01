#pragma once

// The servers this desktop knows ("Your servers"): a club's, a friend's
// self-hosted one, collab.aronwk.com, and so on. Each keeps its own
// sign-in token (TokenStore, keyed by scheme, host and port), its own
// folders of server parts and live layouts (folderName()), what it last
// said about itself (version, features), who is signed in there, and the
// layouts last opened from it. One is the Main server: your settings
// follow your account there (settingsAccount()).
//
// Kept in QSettings as one JSON list. The first load after updating from a
// single-server version takes the address it remembered as the first
// server, marked Main, so nobody signs in again.

#include "ServerApi.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace bld::sync {

struct RecentLayout {
    QString id;
    QString title;
    bool readOnly = false;
    QDateTime openedAt;
};

struct ServerEntry {
    QUrl url;            // the server's base address (ServerApi::normalizeBase)
    QString name;        // what the user calls it; empty: its host
    bool main = false;
    QDateTime lastUsed;  // last connected (invalid: never)
    QString userName;    // who was signed in there, last we heard
    QString version;     // the server's version, last we heard
    std::optional<QStringList> features;  // its features, last we heard (nullopt: unknown or unlisted)
    QList<RecentLayout> recent;  // newest first

    // The same key the token is kept under: scheme, host and port.
    QString key() const;
    // Its name, else its host (with the port when there is one).
    QString label() const;
    // The address as a person reads it: host[:port], no scheme for https.
    QString address() const;
    // What this desktop uses that the server lacked when we last asked.
    QStringList missing() const;
};

class ServerList {
public:
    // Settings keys.
    static constexpr const char* kKey = "serverList/json";
    // Where single-server versions kept the one address.
    static constexpr const char* kLegacyAddressKey = "sync/serverAddress";
    // How many recent layouts each server keeps.
    static constexpr int kMaxRecent = 8;

    // From QSettings (migrating the single-server setting the first time).
    static ServerList load();
    void save() const;

    const QList<ServerEntry>& servers() const { return servers_; }
    bool isEmpty() const { return servers_.isEmpty(); }

    // By address (same scheme, host and port); null when not in the list.
    const ServerEntry* find(const QUrl& url) const;
    ServerEntry* find(const QUrl& url);
    const ServerEntry* mainServer() const;
    // The one connected to last, else Main, else the first.
    const ServerEntry* lastUsed() const;

    // Adds it (or returns the one already there). The first server is Main.
    ServerEntry& add(const QUrl& url, const QString& name = {});
    bool rename(const QUrl& url, const QString& name);
    // Removing Main makes the first one left Main.
    bool remove(const QUrl& url);
    void setMain(const QUrl& url);
    // Connected now: remembered as last used (added when new).
    void touch(const QUrl& url);
    // Opened `layout` from `url`: first in its recent list.
    void addRecent(const QUrl& url, const RecentLayout& layout);
    // What the server said about itself.
    void remember(const QUrl& url, const ServerInfo& info);
    void rememberUser(const QUrl& url, const QString& userName);

    // Whose account your settings follow: the Main server's. Empty when
    // there is none (no servers).
    QUrl settingsAccount() const;

    // The folder name for a server's own data (server-parts/<name>,
    // live/<name>): its host, plus _<port> when it has one, so two
    // servers on one machine never share a folder.
    static QString folderName(const QUrl& url);

private:
    // The single-server setting becomes the first entry, marked Main. A
    // server with a port had its folders named by host alone: they move to
    // folderName() (and the parts library follows).
    static void migrateLegacy(ServerList& list);
    QList<ServerEntry> servers_;
};

}  // namespace bld::sync
