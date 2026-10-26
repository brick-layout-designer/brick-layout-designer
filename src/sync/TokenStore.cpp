#include "TokenStore.h"

#include <qtkeychain/keychain.h>

namespace bld::sync {

QString TokenStore::keyFor(const QUrl& server) {
    return server
        .adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo
                  | QUrl::StripTrailingSlash)
        .toString();
}

void KeychainTokenStore::load(const QUrl& server, std::function<void(const QString&)> done) {
    auto* job = new QKeychain::ReadPasswordJob(service_);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(keyFor(server));
    QObject::connect(job, &QKeychain::Job::finished, [job, done = std::move(done)] {
        done(job->error() == QKeychain::NoError ? job->textData() : QString());
    });
    job->start();
}

void KeychainTokenStore::save(const QUrl& server, const QString& token) {
    auto* job = new QKeychain::WritePasswordJob(service_);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(keyFor(server));
    job->setTextData(token);
    job->start();
}

void KeychainTokenStore::remove(const QUrl& server) {
    auto* job = new QKeychain::DeletePasswordJob(service_);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(keyFor(server));
    job->start();
}

void MigratingTokenStore::load(const QUrl& server, std::function<void(const QString&)> done) {
    current_->load(server, [current = current_, legacy = legacy_, server, done = std::move(done)](const QString& token) {
        if (!token.isEmpty()) {
            done(token);
            return;
        }
        legacy->load(server, [current, server, done](const QString& old) {
            if (!old.isEmpty()) current->save(server, old);
            done(old);
        });
    });
}

void MigratingTokenStore::save(const QUrl& server, const QString& token) {
    current_->save(server, token);
}

void MigratingTokenStore::remove(const QUrl& server) {
    current_->remove(server);
    legacy_->remove(server);
}

std::shared_ptr<TokenStore> MigratingTokenStore::keychain() {
    return std::make_shared<MigratingTokenStore>(
        std::make_shared<KeychainTokenStore>(KeychainTokenStore::kService),
        std::make_shared<KeychainTokenStore>(KeychainTokenStore::kLegacyService));
}

} // namespace bld::sync
