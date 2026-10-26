// Sign-ins saved before the rename (under the keychain's old service name)
// keep working: MigratingTokenStore reads through to them.

#include <gtest/gtest.h>

#include "TokenStore.h"

#include <memory>

using namespace bld::sync;

namespace {

const QUrl kServer(QStringLiteral("https://collab.example.org"));

QString loaded(TokenStore& store) {
    QString got = QStringLiteral("(not called)");
    store.load(kServer, [&](const QString& token) { got = token; });
    return got;
}

struct Stores {
    std::shared_ptr<MemoryTokenStore> current = std::make_shared<MemoryTokenStore>();
    std::shared_ptr<MemoryTokenStore> legacy = std::make_shared<MemoryTokenStore>();
    MigratingTokenStore store{ current, legacy };
};

}  // namespace

TEST(TokenStore, KeychainServicesNameTheAppNowAndBefore) {
    EXPECT_EQ(KeychainTokenStore::kService, QStringLiteral("Collaborative Brick Layout Designer server sign-in"));
    EXPECT_EQ(KeychainTokenStore::kLegacyService, QStringLiteral("Brick Layout Designer server sign-in"));
}

TEST(TokenStore, AnOldSignInIsCopiedOverAndKept) {
    Stores s;
    s.legacy->save(kServer, QStringLiteral("bld_pat_old"));
    EXPECT_EQ(loaded(s.store), QStringLiteral("bld_pat_old"));
    EXPECT_EQ(s.current->tokens.value(TokenStore::keyFor(kServer)), QStringLiteral("bld_pat_old"));
    // An older version, after a downgrade, still finds it.
    EXPECT_EQ(s.legacy->tokens.value(TokenStore::keyFor(kServer)), QStringLiteral("bld_pat_old"));
}

TEST(TokenStore, TheNewSignInWins) {
    Stores s;
    s.legacy->save(kServer, QStringLiteral("bld_pat_old"));
    s.current->save(kServer, QStringLiteral("bld_pat_new"));
    EXPECT_EQ(loaded(s.store), QStringLiteral("bld_pat_new"));
}

TEST(TokenStore, NoSignInAnywhereIsEmpty) {
    Stores s;
    EXPECT_EQ(loaded(s.store), QString());
    EXPECT_TRUE(s.current->tokens.isEmpty());
}

TEST(TokenStore, SavingWritesTheNewServiceAndSigningOutClearsBoth) {
    Stores s;
    s.legacy->save(kServer, QStringLiteral("bld_pat_old"));
    s.store.save(kServer, QStringLiteral("bld_pat_new"));
    EXPECT_EQ(s.current->tokens.value(TokenStore::keyFor(kServer)), QStringLiteral("bld_pat_new"));
    EXPECT_EQ(s.legacy->tokens.value(TokenStore::keyFor(kServer)), QStringLiteral("bld_pat_old"));
    s.store.remove(kServer);
    EXPECT_TRUE(s.current->tokens.isEmpty());
    EXPECT_TRUE(s.legacy->tokens.isEmpty());
    EXPECT_EQ(loaded(s.store), QString());
}
