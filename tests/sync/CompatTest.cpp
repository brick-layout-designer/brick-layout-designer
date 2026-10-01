// The desktop against compat/compat.json, the file the web server's tests
// read too: the same versions, features and answers on both sides.

#include "Compat.h"
#include "ServerApi.h"
#include "core/Sidecar.h"
#include "core/Version.h"
#include "import/LayoutFile.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace bld;
using namespace bld::sync;

namespace {

QJsonObject compat() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/compat/compat.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonArray cases(const char* name) {
    return compat().value(QLatin1String("cases")).toObject().value(QLatin1String(name)).toArray();
}

QStringList strings(const QJsonValue& v) {
    QStringList out;
    for (const auto& s : v.toArray()) out << s.toString();
    return out;
}

QString standingName(Standing s) {
    switch (s) {
    case Standing::Ok: return QStringLiteral("ok");
    case Standing::UpdateSuggested: return QStringLiteral("updateSuggested");
    case Standing::UpdateRequired: return QStringLiteral("updateRequired");
    }
    return {};
}

}  // namespace

TEST(Compat, HoldsTheVersionsThisBuildUses) {
    const QJsonObject c = compat();
    ASSERT_FALSE(c.isEmpty());
    EXPECT_EQ(kDocSchemaVersion, c.value(QLatin1String("doc")).toObject().value(QLatin1String("schemaVersion")).toInt());
    EXPECT_EQ(kDocMinReadable, c.value(QLatin1String("doc")).toObject().value(QLatin1String("minReadable")).toInt());
    EXPECT_EQ(core::Sidecar::kSchemaVersion,
              c.value(QLatin1String("sidecar")).toObject().value(QLatin1String("schemaVersion")).toInt());
    EXPECT_EQ(import::kLayoutFileVersion,
              c.value(QLatin1String("layoutFile")).toObject().value(QLatin1String("version")).toInt());
    EXPECT_EQ(core::desktopDownloadUrl(),
              c.value(QLatin1String("desktop")).toObject().value(QLatin1String("downloadUrl")).toString());
    // The protocols the server speaks are the ones this build needs.
    ServerInfo info;
    info.schemaVersion = kDocSchemaVersion;
    info.protocols = strings(c.value(QLatin1String("protocols")));
    EXPECT_TRUE(info.compatible());
}

TEST(Compat, ThisBuildIsNotBelowWhatServersRecommend) {
    // A release made from this code must not be asked to update by a server
    // built from the same compat file.
    const QJsonObject d = compat().value(QLatin1String("desktop")).toObject();
    const QString mine = QStringLiteral(BLD_TEST_APP_VERSION);
    EXPECT_EQ(desktopStanding(mine, d.value(QLatin1String("minimum")).toString(),
                              d.value(QLatin1String("recommended")).toString()),
              Standing::Ok)
        << mine.toStdString();
}

TEST(Compat, SendsAUserAgentServersRecognise) {
    const QString pattern = compat().value(QLatin1String("desktop")).toObject().value(QLatin1String("userAgentPattern")).toString();
    const QString before = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(QStringLiteral("1.3.0"));
    const auto m = QRegularExpression(pattern).match(QString::fromUtf8(userAgent()));
    QCoreApplication::setApplicationVersion(before);
    ASSERT_TRUE(m.hasMatch());
    EXPECT_EQ(m.captured(1), QStringLiteral("1.3.0"));
}

TEST(Compat, NamesTheFeaturesItUses) {
    const QJsonObject c = compat();
    EXPECT_EQ(desktopFeatures(), strings(c.value(QLatin1String("desktopUses"))));
    EXPECT_EQ(assumedWhenUnlisted(), strings(c.value(QLatin1String("assumedWhenUnlisted"))));
    for (const auto& v : c.value(QLatin1String("features")).toArray()) {
        const QJsonObject f = v.toObject();
        EXPECT_EQ(featureLabel(f.value(QLatin1String("id")).toString()), f.value(QLatin1String("label")).toString());
    }
}

TEST(Compat, ComparesVersionsLikeTheServer) {
    for (const auto& v : cases("versionCompare")) {
        const QJsonObject c = v.toObject();
        const QString a = c.value(QLatin1String("a")).toString(), b = c.value(QLatin1String("b")).toString();
        const auto got = core::compareVersions(a, b);
        if (c.value(QLatin1String("cmp")).isNull()) {
            EXPECT_FALSE(got.has_value()) << a.toStdString() << " vs " << b.toStdString();
        } else {
            ASSERT_TRUE(got.has_value()) << a.toStdString() << " vs " << b.toStdString();
            EXPECT_EQ(*got, c.value(QLatin1String("cmp")).toInt()) << a.toStdString() << " vs " << b.toStdString();
        }
    }
}

TEST(Compat, StandsWhereTheServerSaysItStands) {
    for (const auto& v : cases("standing")) {
        const QJsonObject c = v.toObject();
        EXPECT_EQ(standingName(desktopStanding(c.value(QLatin1String("app")).toString(),
                                               c.value(QLatin1String("minimum")).toString(),
                                               c.value(QLatin1String("recommended")).toString())),
                  c.value(QLatin1String("expect")).toString())
            << c.value(QLatin1String("app")).toString().toStdString();
    }
}

TEST(Compat, FindsTheFeaturesAServerLacks) {
    for (const auto& v : cases("missingFeatures")) {
        const QJsonObject c = v.toObject();
        const QJsonValue server = c.value(QLatin1String("server"));
        const std::optional<QStringList> features =
            server.isNull() ? std::nullopt : std::optional<QStringList>(strings(server));
        EXPECT_EQ(missingFeatures(features), strings(c.value(QLatin1String("expect"))));
    }
}

TEST(Compat, ReadsOnlyDocumentsItUnderstands) {
    for (const auto& v : cases("docReadable")) {
        const QJsonObject c = v.toObject();
        const QJsonValue doc = c.value(QLatin1String("doc"));
        EXPECT_EQ(canReadDoc(doc.isNull() ? std::nullopt : std::optional<int>(doc.toInt()),
                             c.value(QLatin1String("schemaVersion")).toInt(),
                             c.value(QLatin1String("minReadable")).toInt()),
                  c.value(QLatin1String("expect")).toBool())
            << doc.toInt();
    }
}
