// The Parts list's tooltip: who built an imported model (as the web's parts
// panel shows it) and which of your servers a server part came from.

#include "ui/PartsBrowser.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>

using namespace bld;

TEST(PartsTooltip, ALocalPartShowsItsDescriptionAndKey) {
    parts::PartMetadata meta;
    meta.xmlFilePath = QStringLiteral("/home/me/parts/Bricks/3001.1.xml");
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("3001.1"), QStringLiteral("Brick 2 x 4"), {}),
              QStringLiteral("Brick 2 x 4\n(3001.1)"));
}

TEST(PartsTooltip, NamesWhoBuiltAnImportedModel) {
    parts::PartMetadata meta;
    meta.designer = QStringLiteral("Sam Builder");
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("MARKET"), QString(), {}),
              QStringLiteral("MARKET\nDesigned by Sam Builder"));
}

TEST(PartsTooltip, NamesTheServerAServerPartCameFrom) {
    parts::PartMetadata meta;
    meta.xmlFilePath = QStringLiteral("/data/BLD/server-parts/bricks.example.org_8443/custom/X1.xml");
    const QHash<QString, QString> labels{ { QStringLiteral("bricks.example.org_8443"), QStringLiteral("Club server") } };
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("X1"), QString(), labels),
              QStringLiteral("X1\nFrom the server Club server"));
    // A server no longer in the list: its folder name.
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("X1"), QString(), {}),
              QStringLiteral("X1\nFrom the server bricks.example.org_8443"));
}

namespace {
void writePart(const QString& dir, const QString& name) {
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + name + QStringLiteral(".xml"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("<part><Author>t</Author></part>");
    f.close();
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::red);
    ASSERT_TRUE(img.save(dir + QLatin1Char('/') + name + QStringLiteral(".gif.png"), "PNG")
                || img.save(dir + QLatin1Char('/') + name + QStringLiteral(".png"), "PNG"));
}
int listed(ui::PartsBrowser& b) {
    auto* grid = b.findChild<QListWidget*>();
    return grid ? grid->count() : -1;
}
}  // namespace

TEST(PartsBrowserServers, LiveOnAServerOnlyItsServerPartsAreListedUnlessAllAreAskedFor) {
    QTemporaryDir root;
    const QString base = root.path() + QStringLiteral("/server-parts");
    writePart(base + QStringLiteral("/club.example/custom"), QStringLiteral("CLUBPART"));
    writePart(base + QStringLiteral("/friend.example/custom"), QStringLiteral("FRIENDPART"));
    writePart(root.path() + QStringLiteral("/imports"), QStringLiteral("MINE"));
    parts::PartsLibrary lib;
    lib.addSearchPath(base + QStringLiteral("/club.example"));
    lib.addSearchPath(base + QStringLiteral("/friend.example"));
    lib.addSearchPath(root.path() + QStringLiteral("/imports"));
    lib.scan();
    ASSERT_EQ(lib.keys().size(), 3);

    QSettings().setValue(QStringLiteral("parts/showAllServers"), false);
    ui::PartsBrowser b(lib);
    b.rebuild();
    EXPECT_EQ(listed(b), 3);  // not live: everything
    b.setLiveServerFolder(QStringLiteral("club.example"));
    EXPECT_EQ(listed(b), 2);  // the club's part and your own, not the friend's server's
    QSettings().setValue(QStringLiteral("parts/showAllServers"), true);
    b.rebuild();
    EXPECT_EQ(listed(b), 3);
    QSettings().remove(QStringLiteral("parts/showAllServers"));
    b.setLiveServerFolder(QString());
    EXPECT_EQ(listed(b), 3);
}
