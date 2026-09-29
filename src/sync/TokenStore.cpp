#include "TokenStore.h"

#include <qtkeychain/keychain.h>

namespace bld::sync {

namespace {
const QString kService = QStringLiteral("Brick Layout Designer server sign-in");
}

QString TokenStore::keyFor(const QUrl& server) {
    return server
        .adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo
                  | QUrl::StripTrailingSlash)
        .toString();
}

void KeychainTokenStore::load(const QUrl& server, std::function<void(const QString&)> done) {
    auto* job = new QKeychain::ReadPasswordJob(kService);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(keyFor(server));
    QObject::connect(job, &QKeychain::Job::finished, [job, done = std::move(done)] {
        done(job->error() == QKeychain::NoError ? job->textData() : QString());
    });
    job->start();
}

void KeychainTokenStore::save(const QUrl& server, const QString& token) {
    auto* job = new QKeychain::WritePasswordJob(kService);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(keyFor(server));
    job->setTextData(token);
    job->start();
}

void KeychainTokenStore::remove(const QUrl& server) {
    auto* job = new QKeychain::DeletePasswordJob(kService);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(keyFor(server));
    job->start();
}

} // namespace bld::sync
