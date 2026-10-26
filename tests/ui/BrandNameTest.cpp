// The app is called "Collaborative Brick Layout Designer" (Aaron,
// 2026-10-20), on the desktop and on the web. Every name people see says
// so: a source, packaging, help, translation or doc file that says
// "Brick Layout Designer" without "Collaborative " in front fails here,
// also when a string or comment wraps between the words. Tests and fixtures
// are left out (they quote old files), and so is the old user agent
// "BrickLayoutDesigner/x.y", which has no spaces. The web repo has the same
// guard (apps/server/src/brandName.test.ts).

#include <gtest/gtest.h>

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>

namespace {

const QString kRoot = QStringLiteral(BLD_SOURCE_DIR);

// What people read: the sources, the packaging, the help, the translations
// and the docs.
const QStringList kScanned = {
    QStringLiteral("src"),          QStringLiteral("packaging"),  QStringLiteral("help"),
    QStringLiteral("translations"), QStringLiteral("docs"),       QStringLiteral("scripts/help"),
    QStringLiteral("README.md"),    QStringLiteral("CMakeLists.txt"), QStringLiteral(".github/workflows"),
};

// Files that must keep the old name, and why.
const QStringList kExceptions = {
    QStringLiteral("src/core/NameMigration.h"),    // migrates from the old name
    QStringLiteral("src/core/NameMigration.cpp"),  // migrates from the old name
    QStringLiteral("src/sync/TokenStore.h"),       // the keychain's old service name
};

const QStringList kText = { QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.in"),
                            QStringLiteral("*.ts"), QStringLiteral("*.md"), QStringLiteral("*.json"),
                            QStringLiteral("*.html"), QStringLiteral("*.py"), QStringLiteral("*.desktop"),
                            QStringLiteral("*.xml"), QStringLiteral("*.wxs"), QStringLiteral("*.yml"),
                            QStringLiteral("*.txt"), QStringLiteral("*.cmake") };

// Between the words: spaces, line breaks, a comment's `//`, `*` or `#`, or
// the quotes and `+` of a string split over two lines.
const QString kGap = QStringLiteral(R"((?:\s|//|\*|#|["'+])+)");

QMap<QString, QString> sources() {
    QMap<QString, QString> files;
    auto add = [&](const QString& path) {
        const QString rel = path.mid(kRoot.size() + 1);
        if (kExceptions.contains(rel)) return;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) files.insert(rel, QString::fromUtf8(f.readAll()));
    };
    for (const QString& p : kScanned) {
        const QString path = kRoot + QLatin1Char('/') + p;
        if (QFileInfo(path).isFile()) {
            add(path);
            continue;
        }
        QDirIterator it(path, kText, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) add(it.next());
    }
    return files;
}

// Each place in `files` that names the app without "Collaborative".
QStringList oldNames(const QMap<QString, QString>& files) {
    static const QRegularExpression name(QStringLiteral(R"(\bBrick%1Layout%1Designer)").arg(kGap));
    static const QRegularExpression trailingGap(kGap + QLatin1Char('$'));
    QStringList found;
    for (auto f = files.cbegin(); f != files.cend(); ++f) {
        const QString& text = f.value();
        for (auto it = name.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            QString before = text.left(m.capturedStart());
            before.remove(trailingGap);
            if (before.endsWith(QLatin1String("Collaborative"))) continue;
            const int line = int(text.left(m.capturedStart()).count(QLatin1Char('\n'))) + 1;
            found << QStringLiteral("%1:%2: %3").arg(f.key()).arg(line).arg(text.section(QLatin1Char('\n'), line - 1, line - 1).trimmed().left(100));
        }
    }
    return found;
}

}  // namespace

TEST(BrandName, IsTheNewNameWherePeopleReadIt) {
    const auto files = sources();
    // It really looked: the window title, the installers and the help are in there.
    for (const char* must : { "src/ui/MainWindowFileIO.cpp", "packaging/windows/brick-layout-designer.wxs",
                              "packaging/linux/brick-layout-designer.desktop", "help/en/index.html",
                              "translations/bld_de.ts", "README.md" }) {
        EXPECT_TRUE(files.contains(QString::fromLatin1(must))) << must;
    }
    const QStringList found = oldNames(files);
    EXPECT_TRUE(found.isEmpty()) << "Say \"Collaborative Brick Layout Designer\":\n"
                                 << found.join(QLatin1Char('\n')).toStdString();
}

TEST(BrandName, NoticesTheOldNameAlsoWhenItWraps) {
    const QMap<QString, QString> files{
        { QStringLiteral("a"), QStringLiteral("About Brick Layout Designer") },
        { QStringLiteral("b"), QStringLiteral("tr(\"a newer version of Brick Layout \"\n   \"Designer can read\")") },
        { QStringLiteral("c"), QStringLiteral("// made by Brick\n// Layout Designer") },
        { QStringLiteral("d"), QStringLiteral("Welcome to Collaborative Brick Layout Designer") },
        { QStringLiteral("e"), QStringLiteral("tr(\"this version of Collaborative \"\n   \"Brick Layout Designer\")") },
        { QStringLiteral("f"), QStringLiteral("BrickLayoutDesigner/1.3.0 (desktop)") },
        { QStringLiteral("g"), QStringLiteral("brick-layout-designer.desktop") },
        { QStringLiteral("h"), QStringLiteral("the original BlueBrick Layout Designer") },
    };
    EXPECT_EQ(oldNames(files), (QStringList{ QStringLiteral("a:1: About Brick Layout Designer"),
                                             QStringLiteral("b:1: tr(\"a newer version of Brick Layout \""),
                                             QStringLiteral("c:1: // made by Brick") }));
}
