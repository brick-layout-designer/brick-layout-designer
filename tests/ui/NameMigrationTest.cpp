// The first start under the new name brings over the settings and the data
// folder kept under the old one, once, without overwriting anything and
// without breaking the old ones (an older version after a downgrade).
// Uses test names and QStandardPaths' test mode, never the real stores.

#include <gtest/gtest.h>

#include "core/NameMigration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

using namespace bld::core;

namespace {

// Per process, so ctest -j runs the tests side by side without sharing stores.
const QString kPid = QString::number(QCoreApplication::applicationPid());
const AppName kOld{ QStringLiteral("CBLDMigrationTestOld") + kPid, QStringLiteral("Old Name Test") };
const AppName kNew{ QStringLiteral("CBLDMigrationTestNew") + kPid, QStringLiteral("New Name Test") };

QString dataFor(const AppName& name) {
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(name.organization);
    QCoreApplication::setApplicationName(name.application);
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setOrganizationName(org);
    QCoreApplication::setApplicationName(app);
    return path;
}

void removePath(const QString& path) {
    const QFileInfo info(path);
    if (info.isSymLink()) {
        QFile::remove(path);
    } else if (info.isDir()) {
        QDir(path).removeRecursively();
    }
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class NameMigration : public ::testing::Test {
protected:
    void SetUp() override {
        savedOrg_ = QCoreApplication::organizationName();
        savedApp_ = QCoreApplication::applicationName();
        savedTestMode_ = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(kNew.organization);
        QCoreApplication::setApplicationName(kNew.application);
        oldData_ = dataFor(kOld);
        newData_ = dataFor(kNew);
        cleanUp();
    }
    void TearDown() override {
        cleanUp();
        QCoreApplication::setOrganizationName(savedOrg_);
        QCoreApplication::setApplicationName(savedApp_);
        QStandardPaths::setTestModeEnabled(savedTestMode_);
    }
    void cleanUp() {
        for (const AppName& name : { kOld, kNew }) {
            QSettings s(name.organization, name.application);
            s.clear();
            s.sync();
            // An ini file (Linux): leave no per-process folder behind.
            if (s.format() == QSettings::IniFormat || QFileInfo(s.fileName()).isFile()) {
                QFile::remove(s.fileName());
                QDir().rmdir(QFileInfo(s.fileName()).absolutePath());
            }
        }
        removePath(oldData_);
        removePath(newData_);
        // The organization folders above them, when empty.
        QDir().rmdir(QFileInfo(oldData_).absolutePath());
        QDir().rmdir(QFileInfo(newData_).absolutePath());
    }
    // What an older version left: settings with paths into its data folder,
    // and the folder itself.
    void makeOldInstall() {
        QSettings old(kOld.organization, kOld.application);
        old.setValue(QStringLiteral("general/language"), QStringLiteral("de"));
        old.setValue(QStringLiteral("partsLibrary/userPaths"),
                     QStringList{ oldData_ + QStringLiteral("/layout-parts"), QStringLiteral("/elsewhere/parts") });
        old.setValue(QStringLiteral("recent/lastFile"), oldData_ + QStringLiteral("/imports/train.bbm"));
        old.setValue(QStringLiteral("servers/list"),
                     QJsonDocument(QJsonArray{ QJsonObject{
                                       { QStringLiteral("url"), QStringLiteral("https://collab.example.org") },
                                       { QStringLiteral("folder"), oldData_ + QStringLiteral("/live/collab") } } })
                         .toJson(QJsonDocument::Compact));
        old.sync();
        writeFile(oldData_ + QStringLiteral("/venues/hall.json"), "{\"name\":\"Hall\"}");
        writeFile(oldData_ + QStringLiteral("/autosave.bld-layout"), "autosave");
    }

    QString oldData_;
    QString newData_;

private:
    QString savedOrg_;
    QString savedApp_;
    bool savedTestMode_ = false;
};

}  // namespace

TEST(NameMigrationNames, AreTheNewAndTheOldName) {
    EXPECT_EQ(currentAppName().organization, QStringLiteral("CollaborativeBrickLayoutDesigner"));
    EXPECT_EQ(currentAppName().application, QStringLiteral("Collaborative Brick Layout Designer"));
    EXPECT_EQ(legacyAppName().organization, QStringLiteral("BrickLayoutDesigner"));
    EXPECT_EQ(legacyAppName().application, QStringLiteral("Brick Layout Designer"));
}

TEST_F(NameMigration, AFreshInstallOnlyMarksItDone) {
    const NameMigrationReport r = migrateFromName(kOld);
    EXPECT_FALSE(r.alreadyDone);
    EXPECT_FALSE(r.settingsCopied);
    EXPECT_FALSE(r.dataMoved);
    EXPECT_FALSE(r.dataCopied);
    EXPECT_TRUE(r.error.isEmpty()) << r.error.toStdString();
    QSettings now;
    now.setFallbacksEnabled(false);  // only this app's own keys (macOS adds system ones)
    EXPECT_EQ(now.allKeys(), QStringList{ QString::fromLatin1(kNameMigrationDoneKey) });
    EXPECT_FALSE(QFileInfo::exists(newData_));
    EXPECT_TRUE(migrateFromName(kOld).alreadyDone);
}

TEST_F(NameMigration, AnOldInstallMovesOverAndStaysUsable) {
    makeOldInstall();
    const NameMigrationReport r = migrateFromName(kOld);
    EXPECT_TRUE(r.error.isEmpty()) << r.error.toStdString();
    EXPECT_TRUE(r.settingsCopied);
#ifdef Q_OS_WIN
    EXPECT_TRUE(r.dataCopied);
#else
    EXPECT_TRUE(r.dataMoved);
    EXPECT_TRUE(QFileInfo(oldData_).isSymLink());
#endif

    // Settings: copied, with the data folder's paths following it.
    QSettings now;
    EXPECT_EQ(now.value(QStringLiteral("general/language")).toString(), QStringLiteral("de"));
    EXPECT_EQ(now.value(QStringLiteral("partsLibrary/userPaths")).toStringList(),
              (QStringList{ newData_ + QStringLiteral("/layout-parts"), QStringLiteral("/elsewhere/parts") }));
    EXPECT_EQ(now.value(QStringLiteral("recent/lastFile")).toString(), newData_ + QStringLiteral("/imports/train.bbm"));
    const QJsonArray servers = QJsonDocument::fromJson(now.value(QStringLiteral("servers/list")).toByteArray()).array();
    ASSERT_EQ(servers.size(), 1);
    EXPECT_EQ(servers[0].toObject().value(QStringLiteral("folder")).toString(), newData_ + QStringLiteral("/live/collab"));
    EXPECT_EQ(servers[0].toObject().value(QStringLiteral("url")).toString(), QStringLiteral("https://collab.example.org"));

    // Data: under the new name.
    EXPECT_EQ(readFile(newData_ + QStringLiteral("/venues/hall.json")), QByteArray("{\"name\":\"Hall\"}"));
    EXPECT_EQ(readFile(newData_ + QStringLiteral("/autosave.bld-layout")), QByteArray("autosave"));

    // The old ones still work for an older version.
    QSettings old(kOld.organization, kOld.application);
    EXPECT_EQ(old.value(QStringLiteral("general/language")).toString(), QStringLiteral("de"));
    EXPECT_EQ(old.value(QStringLiteral("recent/lastFile")).toString(), oldData_ + QStringLiteral("/imports/train.bbm"));
    EXPECT_EQ(readFile(oldData_ + QStringLiteral("/venues/hall.json")), QByteArray("{\"name\":\"Hall\"}"));
}

TEST_F(NameMigration, BothPresentKeepsTheNewOnes) {
    makeOldInstall();
    {
        QSettings now;
        now.setValue(QStringLiteral("general/language"), QStringLiteral("fr"));
    }
    writeFile(newData_ + QStringLiteral("/mine.txt"), "mine");

    const NameMigrationReport r = migrateFromName(kOld);
    EXPECT_FALSE(r.settingsCopied);
    EXPECT_FALSE(r.dataMoved);
    EXPECT_FALSE(r.dataCopied);
    QSettings now;
    EXPECT_EQ(now.value(QStringLiteral("general/language")).toString(), QStringLiteral("fr"));
    EXPECT_FALSE(now.contains(QStringLiteral("partsLibrary/userPaths")));
    EXPECT_EQ(readFile(newData_ + QStringLiteral("/mine.txt")), QByteArray("mine"));
    EXPECT_FALSE(QFileInfo::exists(newData_ + QStringLiteral("/venues/hall.json")));
    // The old ones are left as they were.
    EXPECT_FALSE(QFileInfo(oldData_).isSymLink());
    EXPECT_EQ(readFile(oldData_ + QStringLiteral("/venues/hall.json")), QByteArray("{\"name\":\"Hall\"}"));
    EXPECT_EQ(QSettings(kOld.organization, kOld.application).value(QStringLiteral("general/language")).toString(),
              QStringLiteral("de"));
}

TEST_F(NameMigration, AnEmptyNewFolderIsFilled) {
    makeOldInstall();
    QDir().mkpath(newData_);
    const NameMigrationReport r = migrateFromName(kOld);
    EXPECT_TRUE(r.dataMoved || r.dataCopied);
    EXPECT_EQ(readFile(newData_ + QStringLiteral("/autosave.bld-layout")), QByteArray("autosave"));
}

TEST_F(NameMigration, RunsOnlyOnce) {
    makeOldInstall();
    migrateFromName(kOld);
    QSettings(kOld.organization, kOld.application).setValue(QStringLiteral("later/key"), 1);
    const NameMigrationReport again = migrateFromName(kOld);
    EXPECT_TRUE(again.alreadyDone);
    EXPECT_FALSE(again.settingsCopied);
    EXPECT_FALSE(QSettings().contains(QStringLiteral("later/key")));
}

TEST(NameMigrationPaths, RewriteOnlyWholeFolders) {
    const QString from = QStringLiteral("/home/a/.local/share/Old/Old App");
    const QString to = QStringLiteral("/home/a/.local/share/New/New App");
    EXPECT_EQ(rewritePathPrefix(from, from, to).toString(), to);
    EXPECT_EQ(rewritePathPrefix(from + QStringLiteral("/imports/x.bbm"), from, to).toString(),
              to + QStringLiteral("/imports/x.bbm"));
    // Another folder that only starts the same way stays.
    EXPECT_EQ(rewritePathPrefix(from + QStringLiteral("2/x"), from, to).toString(), from + QStringLiteral("2/x"));
    EXPECT_EQ(rewritePathPrefix(QStringLiteral("/elsewhere") + from, from, to).toString(),
              QStringLiteral("/elsewhere") + from);
    // Lists, and lists in variants.
    EXPECT_EQ(rewritePathPrefix(QStringList{ from + QStringLiteral("/a"), QStringLiteral("b") }, from, to).toStringList(),
              (QStringList{ to + QStringLiteral("/a"), QStringLiteral("b") }));
    EXPECT_EQ(rewritePathPrefix(QVariantList{ from, 3 }, from, to).toList(), (QVariantList{ to, 3 }));
    // JSON bytes follow; binary state and other types are left alone.
    const QByteArray json = "[{\"p\":\"" + from.toUtf8() + "/live\"},\"" + from.toUtf8() + "\"]";
    EXPECT_EQ(rewritePathPrefix(json, from, to).toByteArray(),
              QByteArray("[{\"p\":\"" + to.toUtf8() + "/live\"},\"" + to.toUtf8() + "\"]"));
    const QByteArray binary = QByteArray("\x01\xd9\xd0\xcb", 4) + from.toUtf8();
    EXPECT_EQ(rewritePathPrefix(binary, from, to).toByteArray(), binary);
    EXPECT_EQ(rewritePathPrefix(42, from, to).toInt(), 42);
}

TEST(NameMigrationPaths, RewriteTheWindowsSpellingToo) {
    const QString from = QStringLiteral("C:/Users/a/AppData/Roaming/Old/Old App");
    const QString to = QStringLiteral("C:/Users/a/AppData/Roaming/New/New App");
    const QString native = QDir::toNativeSeparators(from) + QDir::separator() + QStringLiteral("imports");
    EXPECT_EQ(rewritePathPrefix(native, from, to).toString(),
              QDir::toNativeSeparators(to) + QDir::separator() + QStringLiteral("imports"));
}
