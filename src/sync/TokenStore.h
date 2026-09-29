#pragma once

// Where the desktop keeps its server sign-in token (a `bld_pat_` personal
// access token), one per server address. The app uses the OS keychain;
// tests use MemoryTokenStore. Reads and writes may finish later (the
// keychain asks the OS), so results come back through callbacks.

#include <QHash>
#include <QString>
#include <QUrl>

#include <functional>

namespace bld::sync {

class TokenStore {
public:
    virtual ~TokenStore() = default;
    // `done` gets the saved token, or an empty string when there is none.
    virtual void load(const QUrl& server, std::function<void(const QString&)> done) = 0;
    virtual void save(const QUrl& server, const QString& token) = 0;
    virtual void remove(const QUrl& server) = 0;

    // The key a server's token is kept under: its scheme, host and port.
    static QString keyFor(const QUrl& server);
};

// macOS Keychain, Windows Credential Store, Secret Service on Linux
// (QtKeychain). Never falls back to storing the token in plain text.
class KeychainTokenStore : public TokenStore {
public:
    void load(const QUrl& server, std::function<void(const QString&)> done) override;
    void save(const QUrl& server, const QString& token) override;
    void remove(const QUrl& server) override;
};

class MemoryTokenStore : public TokenStore {
public:
    void load(const QUrl& server, std::function<void(const QString&)> done) override {
        done(tokens.value(keyFor(server)));
    }
    void save(const QUrl& server, const QString& token) override { tokens.insert(keyFor(server), token); }
    void remove(const QUrl& server) override { tokens.remove(keyFor(server)); }
    QHash<QString, QString> tokens;
};

} // namespace bld::sync
