// Version comparison for the update check.

#include "ui/UpdateCheck.h"

#include <gtest/gtest.h>

using bld::ui::isNewerVersion;

TEST(UpdateCheck, ComparesReleaseVersions) {
    EXPECT_TRUE(isNewerVersion(QStringLiteral("v1.3.0"), QStringLiteral("1.2.0")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.10.0"), QStringLiteral("1.9.9")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("2.0"), QStringLiteral("1.99.99")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("v1.2.0"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.2"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.1.9"), QStringLiteral("1.2.0")));
}

TEST(UpdateCheck, PreReleasesAndJunk) {
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.3.0"), QStringLiteral("1.3.0-beta.2")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.3.0-beta.1"), QStringLiteral("1.3.0")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.4.0-rc.1"), QStringLiteral("1.3.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("nightly"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.3.0"), QStringLiteral("")));
    EXPECT_FALSE(isNewerVersion(QString(), QStringLiteral("1.2.0")));
}
