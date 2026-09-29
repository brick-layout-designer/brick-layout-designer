// Venue model v2 (the web repo's references/VENUE-MODEL.md): the shared
// Grand Lobby fixture round-trips to the same JSON (the web checks the same
// file), unknown fields survive at every level, and unset optional parts
// stay out of the file.

#include "saveload/VenueJson.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include "core/Sidecar.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace bld;

namespace {

QJsonObject fixture() {
    QFile f(QStringLiteral(BLD_VENUE_FIXTURE));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace

TEST(VenueJson, SharedGrandLobbyFixtureRoundTripsExactly) {
    const auto v = saveload::readVenueFile(QStringLiteral(BLD_VENUE_FIXTURE));
    ASSERT_TRUE(v);
    int floor = 0;
    for (const auto& p : v->power) floor += p.floor ? 1 : 0;
    EXPECT_EQ(floor, 3);
    bool stairs = false;
    for (const auto& o : v->obstacles)
        if (o.kind == core::ObstacleKind::Stairs) {
            stairs = true;
            EXPECT_EQ(o.upDegrees, 270.0);
        }
    EXPECT_TRUE(stairs);
    EXPECT_EQ(v->notes.size(), 2);
    EXPECT_EQ(v->dimensions.size(), 6);

    QTemporaryDir dir;
    const QString out = QDir(dir.path()).filePath(QStringLiteral("again.bld-venue"));
    ASSERT_TRUE(saveload::writeVenueFile(out, *v));
    QFile f(out);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(QJsonDocument::fromJson(f.readAll()).object(), fixture());

    // The sidecar carries the same venue JSON.
    core::Sidecar sc;
    sc.venue = *v;
    QJsonObject expected = fixture();
    expected.remove(QStringLiteral("schema"));
    EXPECT_EQ(saveload::sidecarToJson(sc).value(QLatin1String("venue")).toObject(), expected);
}

TEST(VenueJson, KeepsUnknownFieldsAtEveryLevel) {
    const QJsonObject newer{
        { QStringLiteral("name"), QStringLiteral("Hall") },
        { QStringLiteral("floorColor"), QStringLiteral("#ccc") },
        { QStringLiteral("edges"),
          QJsonArray{ QJsonObject{ { QStringLiteral("kind"), 0 },
                                   { QStringLiteral("material"), QStringLiteral("glass") } } } },
        { QStringLiteral("obstacles"),
          QJsonArray{ QJsonObject{ { QStringLiteral("kind"), QStringLiteral("fountain") },
                                   { QStringLiteral("heightStuds"), 40 } } } },
        { QStringLiteral("power"),
          QJsonArray{ QJsonObject{ { QStringLiteral("kind"), QStringLiteral("floor") },
                                   { QStringLiteral("phase"), 3 } } } },
        { QStringLiteral("notes"),
          QJsonArray{ QJsonObject{ { QStringLiteral("text"), QStringLiteral("n") },
                                   { QStringLiteral("author"), QStringLiteral("me") } } } },
        { QStringLiteral("dimensions"),
          QJsonArray{ QJsonObject{ { QStringLiteral("style"), QStringLiteral("arrow") } } } },
    };
    const QJsonObject out = saveload::venueToJson(saveload::venueFromJson(newer));
    EXPECT_EQ(out.value(QLatin1String("floorColor")).toString(), QStringLiteral("#ccc"));
    EXPECT_EQ(
        out.value(QLatin1String("edges")).toArray()[0].toObject().value(QLatin1String("material")).toString(),
        QStringLiteral("glass"));
    const QJsonObject ob = out.value(QLatin1String("obstacles")).toArray()[0].toObject();
    // An unknown kind reads as "other"; its other fields stay.
    EXPECT_FALSE(ob.contains(QLatin1String("kind")));
    EXPECT_EQ(ob.value(QLatin1String("heightStuds")).toInt(), 40);
    const QJsonObject p = out.value(QLatin1String("power")).toArray()[0].toObject();
    EXPECT_EQ(p.value(QLatin1String("phase")).toInt(), 3);
    EXPECT_EQ(p.value(QLatin1String("kind")).toString(), QStringLiteral("floor"));
    EXPECT_FALSE(p.contains(QLatin1String("label")));
    EXPECT_FALSE(p.contains(QLatin1String("amps")));
    EXPECT_EQ(
        out.value(QLatin1String("notes")).toArray()[0].toObject().value(QLatin1String("author")).toString(),
        QStringLiteral("me"));
    EXPECT_EQ(out.value(QLatin1String("dimensions"))
                  .toArray()[0]
                  .toObject()
                  .value(QLatin1String("style"))
                  .toString(),
              QStringLiteral("arrow"));
}

TEST(VenueJson, AVenueWithoutTheNewPartsWritesNoneOfThem) {
    core::Venue v;
    v.name = QStringLiteral("Hall");
    v.edges.push_back({ { QPointF(0, 0), QPointF(10, 0) }, core::EdgeKind::Wall, 0.0, QStringLiteral("w") });
    v.obstacles.push_back({ { QPointF(1, 1), QPointF(2, 1), QPointF(2, 2) }, QStringLiteral("pillar") });
    const QJsonObject o = saveload::venueToJson(v);
    EXPECT_EQ(o.keys(), (QStringList{ QStringLiteral("bounds"), QStringLiteral("edges"),
                                      QStringLiteral("enabled"), QStringLiteral("minWalkwayStuds"),
                                      QStringLiteral("name"), QStringLiteral("obstacles") }));
    EXPECT_EQ(o.value(QLatin1String("edges")).toArray()[0].toObject().keys().size(), 4);
    EXPECT_EQ(o.value(QLatin1String("obstacles")).toArray()[0].toObject().keys().size(), 2);
}
