#include "saveload/SidecarIO.h"

#include "core/Sidecar.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

using namespace bld;

TEST(Sidecar, RoundTripEmptyReports) {
    core::Sidecar sc;
    QTemporaryDir dir;
    const QString path = dir.filePath("t.bbm.bld");
    const QByteArray bbm = "<dummy bbm>";
    QString err;
    ASSERT_TRUE(saveload::writeSidecar(path, bbm, sc, &err)) << err.toStdString();

    core::Sidecar back;
    auto r = saveload::readSidecar(path, bbm, back);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_FALSE(r.hashMismatch);
    EXPECT_TRUE(back.isEmpty());
}

TEST(Sidecar, RoundTripAnchoredLabels) {
    core::Sidecar sc;
    core::AnchoredLabel a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.text = QStringLiteral("Station A");
    a.font.familyName = QStringLiteral("Arial");
    a.font.sizePt = 12.0f;
    a.font.styleString = QStringLiteral("Bold");
    a.color = core::ColorSpec::fromArgb(QColor(200, 100, 50));
    a.kind = core::AnchorKind::Brick;
    a.targetId = QStringLiteral("brick-guid-123");
    a.offset = QPointF(5.5, -3.25);
    a.offsetRotation = 45.0f;
    a.minZoom = 0.5;
    sc.anchoredLabels.push_back(a);

    QTemporaryDir dir;
    const QString path = dir.filePath("a.bbm.bld");
    const QByteArray bbm = "bbm-bytes";
    ASSERT_TRUE(saveload::writeSidecar(path, bbm, sc));

    core::Sidecar back;
    auto r = saveload::readSidecar(path, bbm, back);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(back.anchoredLabels.size(), 1u);
    const auto& b = back.anchoredLabels[0];
    EXPECT_EQ(b.id, a.id);
    EXPECT_EQ(b.text, a.text);
    EXPECT_EQ(b.font.familyName, a.font.familyName);
    EXPECT_FLOAT_EQ(b.font.sizePt, a.font.sizePt);
    EXPECT_EQ(b.font.styleString, a.font.styleString);
    EXPECT_EQ(b.color.color.rgba(), a.color.color.rgba());
    EXPECT_EQ(b.kind, a.kind);
    EXPECT_EQ(b.targetId, a.targetId);
    EXPECT_EQ(b.offset, a.offset);
    EXPECT_FLOAT_EQ(b.offsetRotation, a.offsetRotation);
    EXPECT_DOUBLE_EQ(b.minZoom, a.minZoom);
}

TEST(Sidecar, RoundTripModules) {
    core::Sidecar sc;
    core::Module m;
    m.id = QStringLiteral("mod-1");
    m.name = QStringLiteral("Main station");
    m.memberIds.insert(QStringLiteral("brick-a"));
    m.memberIds.insert(QStringLiteral("brick-b"));
    m.transform = QTransform().translate(100, 50).rotate(30);
    m.sourceFile = QStringLiteral("/path/to/station.bbm");
    m.importedAt = QDateTime::currentDateTimeUtc();
    sc.modules.push_back(m);

    QTemporaryDir dir;
    const QString path = dir.filePath("m.bbm.bld");
    const QByteArray bbm = "x";
    ASSERT_TRUE(saveload::writeSidecar(path, bbm, sc));

    core::Sidecar back;
    ASSERT_TRUE(saveload::readSidecar(path, bbm, back).ok);
    ASSERT_EQ(back.modules.size(), 1u);
    const auto& bm = back.modules[0];
    EXPECT_EQ(bm.id, m.id);
    EXPECT_EQ(bm.name, m.name);
    EXPECT_EQ(bm.memberIds, m.memberIds);
    EXPECT_EQ(bm.transform, m.transform);
    EXPECT_EQ(bm.sourceFile, m.sourceFile);
    // importedAt is stored at second-level precision via ISODate.
    EXPECT_EQ(bm.importedAt.toString(Qt::ISODate), m.importedAt.toString(Qt::ISODate));
}

TEST(Sidecar, RoundTripVenue) {
    core::Sidecar sc;
    core::Venue v;
    v.name = QStringLiteral("Spring Show Hall A");
    v.minWalkwayStuds = 96.0;
    v.layoutBoundsStuds = QRectF(-100, -50, 400, 200);

    core::VenueEdge e1;
    e1.kind = core::EdgeKind::Wall;
    e1.polyline = { QPointF(0, 0), QPointF(300, 0) };
    e1.label = QStringLiteral("North wall");
    v.edges.append(e1);

    core::VenueEdge e2;
    e2.kind = core::EdgeKind::Door;
    e2.polyline = { QPointF(300, 0), QPointF(300, 15) };
    e2.doorWidthStuds = 15.0;
    e2.label = QStringLiteral("Main entrance");
    v.edges.append(e2);

    core::VenueObstacle ob;
    ob.polygon = { QPointF(150, 100), QPointF(170, 100), QPointF(170, 120), QPointF(150, 120) };
    ob.label = QStringLiteral("Pillar A");
    v.obstacles.append(ob);

    sc.venue = v;

    QTemporaryDir dir;
    const QString path = dir.filePath("v.bbm.bld");
    const QByteArray bbm = "y";
    ASSERT_TRUE(saveload::writeSidecar(path, bbm, sc));

    core::Sidecar back;
    ASSERT_TRUE(saveload::readSidecar(path, bbm, back).ok);
    ASSERT_TRUE(back.venue.has_value());
    EXPECT_EQ(back.venue->name, v.name);
    EXPECT_DOUBLE_EQ(back.venue->minWalkwayStuds, v.minWalkwayStuds);
    EXPECT_EQ(back.venue->layoutBoundsStuds, v.layoutBoundsStuds);
    ASSERT_EQ(back.venue->edges.size(), 2);
    EXPECT_EQ(back.venue->edges[0].kind, core::EdgeKind::Wall);
    EXPECT_EQ(back.venue->edges[1].kind, core::EdgeKind::Door);
    EXPECT_DOUBLE_EQ(back.venue->edges[1].doorWidthStuds, 15.0);
    ASSERT_EQ(back.venue->obstacles.size(), 1);
    EXPECT_EQ(back.venue->obstacles[0].label, QStringLiteral("Pillar A"));
}

TEST(Sidecar, HashMismatchDetected) {
    core::Sidecar sc;
    sc.anchoredLabels.push_back({ .id = QStringLiteral("x"), .text = QStringLiteral("hi") });

    QTemporaryDir dir;
    const QString path = dir.filePath("h.bbm.bld");
    ASSERT_TRUE(saveload::writeSidecar(path, "original bbm bytes", sc));

    core::Sidecar back;
    auto r = saveload::readSidecar(path, "modified bbm bytes", back);
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.hashMismatch);
}

// Saved views (references/LAYOUT-FILE.md "Saved views"): in list order,
// with every field, and the fields this build doesn't know kept.
TEST(Sidecar, RoundTripSavedViews) {
    core::Sidecar sc;
    core::SavedView whole;
    whole.id = QStringLiteral("view-whole");
    whole.name = QStringLiteral("Whole layout");
    core::SavedView station;
    station.id = QStringLiteral("view-station");
    station.name = QStringLiteral("Station");
    station.fit = false;
    station.rect = QRectF(90, 40, 40, 30.5);
    station.sheets = QStringList{ QStringLiteral("sheet-town"), QStringLiteral("sheet-track") };
    station.grid = false;
    station.labels = false;
    station.extras.insert(QStringLiteral("zoomHint"), 2.5);
    station.extras.insert(QStringLiteral("future"), QJsonObject{ { QStringLiteral("a"), 1 } });
    sc.views = { whole, station };
    EXPECT_FALSE(sc.isEmpty());

    QTemporaryDir dir;
    const QString path = dir.filePath("t.bbm.bld");
    ASSERT_TRUE(saveload::writeSidecar(path, "<bbm>", sc));
    core::Sidecar back;
    ASSERT_TRUE(saveload::readSidecar(path, "<bbm>", back).ok);
    ASSERT_EQ(back.views.size(), 2u);
    EXPECT_EQ(back.views[0], whole);
    EXPECT_EQ(back.views[1], station);
    EXPECT_EQ(back.views[1].extras.value(QLatin1String("zoomHint")).toDouble(), 2.5);

    // The JSON is the web's shape: fit views write rect and all-sheets as null.
    const QJsonObject json = saveload::viewToJson(whole);
    EXPECT_TRUE(json.value(QLatin1String("rect")).isNull());
    EXPECT_TRUE(json.value(QLatin1String("sheets")).isNull());
    EXPECT_EQ(json.value(QLatin1String("fit")).toBool(), true);
    // A fit view that still remembers an area writes it as null too.
    core::SavedView fitted = station;
    fitted.fit = true;
    EXPECT_TRUE(saveload::viewToJson(fitted).value(QLatin1String("rect")).isNull());
    const QJsonObject rect = saveload::viewToJson(station).value(QLatin1String("rect")).toObject();
    EXPECT_EQ(rect.value(QLatin1String("x")).toDouble(), 90.0);
    EXPECT_EQ(rect.value(QLatin1String("h")).toDouble(), 30.5);
}

// Reading is forgiving, like the web's readView: missing fields take their
// defaults, [x, y, w, h] works too, and a view with no id is dropped.
TEST(Sidecar, ReadsViewsWrittenByOtherBuilds) {
    const QJsonArray views{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("a") } },
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("b") },
                     { QStringLiteral("rect"), QJsonArray{ 1, 2, 3, 4 } } },
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("c") }, { QStringLiteral("fit"), false } },
        QJsonObject{ { QStringLiteral("name"), QStringLiteral("no id") } },
        QStringLiteral("not a view"),
    };
    core::Sidecar sc;
    saveload::sidecarFromJson(QJsonObject{ { QStringLiteral("views"), views } }, sc);
    ASSERT_EQ(sc.views.size(), 3u);
    EXPECT_TRUE(sc.views[0].fit);
    EXPECT_TRUE(sc.views[0].grid);
    EXPECT_TRUE(sc.views[0].labels);
    EXPECT_FALSE(sc.views[0].sheets.has_value());
    // An area and no "fit": it uses the area.
    EXPECT_FALSE(sc.views[1].fit);
    EXPECT_EQ(sc.views[1].rect, QRectF(1, 2, 3, 4));
    // "Use this area" with no usable area can only fit.
    EXPECT_TRUE(sc.views[2].fit);
    // No views: none written.
    EXPECT_FALSE(saveload::sidecarToJson(core::Sidecar{}).contains(QLatin1String("views")));
}

// The map's ids renumbered on reading (BbmReader's renamedIds): the
// sidecar's references follow, and the rest are left alone.
TEST(Sidecar, ReferencesFollowRenumberedIds) {
    core::Sidecar sc;
    core::SavedView v;
    v.id = QStringLiteral("v");
    v.sheets = QStringList{ QStringLiteral("sheet-town"), QStringLiteral("42") };
    sc.views = { v, core::SavedView{} };
    core::AnchoredLabel l;
    l.targetId = QStringLiteral("brick-a");
    sc.anchoredLabels = { l };
    core::Module m;
    m.memberIds = { QStringLiteral("brick-a"), QStringLiteral("7") };
    sc.modules = { m };
    saveload::renameSidecarIds(sc, { { QStringLiteral("sheet-town"), QStringLiteral("111") },
                                     { QStringLiteral("brick-a"), QStringLiteral("222") } });
    EXPECT_EQ(sc.views[0].sheets, (QStringList{ QStringLiteral("111"), QStringLiteral("42") }));
    EXPECT_FALSE(sc.views[1].sheets);
    EXPECT_EQ(sc.anchoredLabels[0].targetId, QStringLiteral("222"));
    EXPECT_EQ(sc.modules[0].memberIds, (QSet<QString>{ QStringLiteral("222"), QStringLiteral("7") }));
}
