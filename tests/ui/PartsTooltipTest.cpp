// The Parts list's tooltip: who built an imported model (as the web's parts
// panel shows it) and which of your servers a server part came from.

#include "ui/PartsBrowser.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

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
