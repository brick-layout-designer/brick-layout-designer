#pragma once

// Where the desktop keeps its server sign-in token (a `bld_pat_` personal
// access token), one per server address. The app uses the OS keychain;
// tests use MemoryTokenStore. Reads and writes may finish later (the
// keychain asks the OS), so results come back through callbacks.

#include <QHash>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

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
    // The keychain service the tokens are filed under, and the one the app
    // used before it was called Collaborative Brick Layout Designer.
    static inline const QString kService = QStringLiteral("Collaborative Brick Layout Designer server sign-in");
    static inline const QString kLegacyService = QStringLiteral("Brick Layout Designer server sign-in");

    explicit KeychainTokenStore(QString service = kService) : service_(std::move(service)) {}
    void load(const QUrl& server, std::function<void(const QString&)> done) override;
    void save(const QUrl& server, const QString& token) override;
    void remove(const QUrl& server) override;

private:
    QString service_;
};

// Reads through to the store an older version of the app wrote: a token
// found only in `legacy` is copied to `current` and returned. It stays in
// `legacy` too, so an older version still signs in after a downgrade.
// Saving writes `current`; removing (signing out) clears both.
class MigratingTokenStore : public TokenStore {
public:
    MigratingTokenStore(std::shared_ptr<TokenStore> current, std::shared_ptr<TokenStore> legacy)
        : current_(std::move(current)), legacy_(std::move(legacy)) {}
    void load(const QUrl& server, std::function<void(const QString&)> done) override;
    void save(const QUrl& server, const QString& token) override;
    void remove(const QUrl& server) override;

    // The app's store: the keychain under the new service name, reading
    // through to the old one.
    static std::shared_ptr<TokenStore> keychain();

private:
    std::shared_ptr<TokenStore> current_;
    std::shared_ptr<TokenStore> legacy_;
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
