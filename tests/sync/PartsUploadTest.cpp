// Desktop parts to the server (sync phase P4b): the user's parts the
// server's catalog lacks are listed, and only the ones they confirm are
// uploaded, personal or to an organisation.

#include "PartsUpload.h"
#include "FakeHttp.h"
#include "ServerApi.h"
#include "UploadPartsDialog.h"

#include <gtest/gtest.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

#include <functional>

using namespace bld::sync;
using bld::synctest::FakeHttp;

namespace {
bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}
void write(const QString& path, const QByteArray& data) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}
} // namespace

TEST(PartsUpload, ListsOnlyPartsTheServerLacksAndUploadsWhatWasChosen) {
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("3001.8.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("3001.8.gif")), "GIF");
    write(dir.filePath(QStringLiteral("MY.1.xml")),
          "<part><Description><en>My bridge</en></Description></part>");
    write(dir.filePath(QStringLiteral("MY.1.png")), "PNG");
    write(dir.filePath(QStringLiteral("MY.2.xml")),
          "<part><Description><en>My tunnel</en></Description></part>");
    write(dir.filePath(QStringLiteral("MY.2.gif")), "GIF2");
    write(dir.filePath(QStringLiteral("NOSPRITE.xml")), "<part/>");
    const auto local = PartsUpload::scanFolder(dir.path());
    ASSERT_EQ(local.size(), 3); // the one without a sprite is skipped
    EXPECT_EQ(local[1].displayName, QStringLiteral("My bridge"));

    FakeHttp http;
    http.reply("/api/parts/catalog", 200,
               { { QStringLiteral("parts"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("key"), QStringLiteral("3001.8") },
                                            { QStringLiteral("partNumber"), QStringLiteral("3001") } } } } });
    http.reply("/api/orgs", 200,
               { { QStringLiteral("orgs"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("club") },
                                            { QStringLiteral("name"), QStringLiteral("Train Club") } } } } });
    http.reply("/api/custom-parts", 201, { { QStringLiteral("id"), QStringLiteral("p1") } });
    PartsUpload upload(http.base(), QStringLiteral("bld_pat_abc"));
    QList<LocalPart> missing;
    bool got = false;
    QObject::connect(&upload, &PartsUpload::missingReady, [&](const QList<LocalPart>& m) {
        missing = m;
        got = true;
    });
    upload.findMissing(local);
    ASSERT_TRUE(waitFor([&] { return got; }));
    ASSERT_EQ(missing.size(), 2);
    EXPECT_EQ(missing[0].key, QStringLiteral("MY.1"));

    // Confirmed: only the checked one goes, to the chosen organisation.
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_abc"));
    UploadPartsDialog dialog(api, upload, missing);
    auto* owner = dialog.findChild<QComboBox*>(QStringLiteral("owner"));
    ASSERT_TRUE(waitFor([&] { return owner->count() == 2; }));
    owner->setCurrentIndex(1);
    dialog.findChild<QListWidget*>(QStringLiteral("parts"))->item(1)->setCheckState(Qt::Unchecked);
    dialog.uploadChecked();
    ASSERT_TRUE(waitFor([&] { return dialog.result() == QDialog::Accepted; }));
    EXPECT_EQ(dialog.uploadedCount(), 1);
    int posts = 0;
    for (const auto& r : http.requests) {
        if (r.path != "/api/custom-parts") continue;
        ++posts;
        const auto body = QJsonDocument::fromJson(r.body).object();
        EXPECT_EQ(body.value(QLatin1String("partNumber")).toString(), QStringLiteral("MY.1"));
        EXPECT_EQ(body.value(QLatin1String("displayName")).toString(), QStringLiteral("My bridge"));
        EXPECT_EQ(body.value(QLatin1String("spriteMime")).toString(), QStringLiteral("image/png"));
        EXPECT_EQ(QByteArray::fromBase64(body.value(QLatin1String("spriteBase64")).toString().toLatin1()),
                  QByteArray("PNG"));
        EXPECT_EQ(body.value(QLatin1String("orgSlug")).toString(), QStringLiteral("club"));
        EXPECT_EQ(r.authorization, QByteArray("Bearer bld_pat_abc"));
    }
    EXPECT_EQ(posts, 1);
}

TEST(PartsUpload, StopsWhenTheTokenCantUpload) {
    FakeHttp http;
    http.reply("/api/custom-parts", 403,
               { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("A.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("A.gif")), "G");
    write(dir.filePath(QStringLiteral("B.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("B.gif")), "G");
    PartsUpload upload(http.base(), QStringLiteral("t"));
    int count = -1;
    QStringList failures;
    bool unauthorized = false;
    QObject::connect(&upload, &PartsUpload::failed, [&](const QString&, bool u) { unauthorized = u; });
    QObject::connect(&upload, &PartsUpload::uploaded, [&](int c, const QStringList& f) {
        count = c;
        failures = f;
    });
    upload.upload(PartsUpload::scanFolder(dir.path()), {});
    ASSERT_TRUE(waitFor([&] { return count >= 0; }));
    EXPECT_EQ(count, 0);
    EXPECT_EQ(failures.size(), 1); // stopped after the first refusal
    EXPECT_TRUE(unauthorized);
}
