// Grid snap (core/GridSnap.h): the cases the web runs too
// (fixtures/grid-snap-vectors.json, from vanilla BlueBrick under Wine), the
// <SnapMargin>s of the real library, then each kind of item on the grid in
// a MapView: parts dragged, placed and dropped as modules, rulers drawn,
// moved and stretched, labels moved, text added and venue corners drawn.

#include "core/GridSnap.h"
#include "core/AnchoredLabel.h"
#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "parts/BrickPlacement.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/theme/AppPrefs.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

using namespace bld;
using namespace bld::ui;

namespace {

QJsonObject vectors() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/grid-snap-vectors.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

QPointF pointOf(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return { a[0].toDouble(), a[1].toDouble() };
}

gridsnap::Margin marginOf(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return { a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble() };
}

QString caseName(const QJsonObject& c) {
    return QString::fromUtf8(QJsonDocument(c).toJson(QJsonDocument::Compact));
}

}  // namespace

TEST(GridSnap, SharedOffsetCases) {
    const QJsonArray cases = vectors()[QLatin1String("offsets")].toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QPointF got = gridsnap::snapOffset(marginOf(c[QLatin1String("margin")]),
                                                 c[QLatin1String("orientation")].toDouble());
        const QPointF want = pointOf(c[QLatin1String("offset")]);
        EXPECT_NEAR(got.x(), want.x(), 1e-5) << caseName(c).toStdString();
        EXPECT_NEAR(got.y(), want.y(), 1e-5) << caseName(c).toStdString();
    }
}

TEST(GridSnap, SharedDragCases) {
    const QJsonObject all = vectors();
    QHash<QString, gridsnap::Margin> margins;
    for (const QJsonValue& v : all[QLatin1String("offsets")].toArray())
        margins.insert(v[QLatin1String("part")].toString(), marginOf(v[QLatin1String("margin")]));
    const QJsonArray cases = all[QLatin1String("drags")].toArray();
    ASSERT_GT(cases.size(), 200);
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QJsonArray area = c[QLatin1String("area")].toArray();
        const QPointF topLeft(area[0].toDouble(), area[1].toDouble());
        const QSizeF size(area[2].toDouble(), area[3].toDouble());
        const QPointF offset = gridsnap::snapOffset(margins.value(c[QLatin1String("part")].toString()),
                                                    c[QLatin1String("orientation")].toDouble());
        const QPointF grab = pointOf(c[QLatin1String("grab")]);
        const QPointF mouse = pointOf(c[QLatin1String("mouse")]);
        // Where the pointer has the corner, then the grid.
        const QPointF raw = topLeft + offset + (mouse - grab);
        const QPointF corner = gridsnap::dragCorner(mouse, raw, c[QLatin1String("grid")].toDouble());
        const QPointF centre = corner - offset + QPointF(size.width() / 2, size.height() / 2);
        const QPointF want = pointOf(c[QLatin1String("centre")]);
        EXPECT_NEAR(centre.x(), want.x(), 1e-3) << caseName(c).toStdString();
        EXPECT_NEAR(centre.y(), want.y(), 1e-3) << caseName(c).toStdString();
    }
}

TEST(GridSnap, SharedPointCases) {
    const QJsonArray cases = vectors()[QLatin1String("points")].toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QPointF got = gridsnap::snapPoint(pointOf(c[QLatin1String("point")]), c[QLatin1String("grid")].toDouble());
        const QPointF want = pointOf(c[QLatin1String("snapped")]);
        EXPECT_NEAR(got.x(), want.x(), 1e-9) << caseName(c).toStdString();
        EXPECT_NEAR(got.y(), want.y(), 1e-9) << caseName(c).toStdString();
    }
}

TEST(GridSnap, HalfwayGoesToTheEvenStep) {
    EXPECT_EQ(gridsnap::roundToStep(12, 8), 16);
    EXPECT_EQ(gridsnap::roundToStep(4, 8), 0);
    EXPECT_EQ(gridsnap::roundToStep(-2.5, 1), -2);
    EXPECT_EQ(gridsnap::roundToStep(-3.5, 1), -4);
    EXPECT_EQ(gridsnap::roundToStep(4.01, 8), 8);
}

TEST(GridSnap, AWholeStepApartIsOnTheLine) {
    // 0.1 + 0.2 is a hair under 0.3: still the line at 0.3 steps of 0.1.
    EXPECT_NEAR(gridsnap::floorToStep(0.1 + 0.2, 0.1), 0.3, 1e-12);
    EXPECT_EQ(gridsnap::floorToStep(-0.5, 1), -1);
    EXPECT_EQ(gridsnap::dragCorner({ 3, 3 }, { 1.5, 1.5 }, 0), QPointF(1.5, 1.5));
}

// The real library: the parts the vectors use read their <SnapMargin>, and
// their display areas are the size vanilla gives them.
TEST(GridSnap, TheLibraryReadsSnapMargins) {
    const QString root = QStringLiteral(BLD_PARTS_LIBRARY_ROOT);
    if (!QFile::exists(root + QStringLiteral("/Track/2865.8.xml"))) GTEST_SKIP() << "no parts library";
    parts::PartsLibrary lib;
    lib.addSearchPath(root);
    lib.scan();
    const QJsonObject all = vectors();
    for (const QJsonValue& v : all[QLatin1String("offsets")].toArray()) {
        const QString key = v[QLatin1String("part")].toString();
        if (key == QLatin1String("IMPORTTEST.1")) continue;  // made for the oracle only
        const auto meta = lib.metadata(key);
        ASSERT_TRUE(meta) << key.toStdString();
        EXPECT_EQ(meta->snapMargin, marginOf(v[QLatin1String("margin")])) << key.toStdString();
    }
    for (const QJsonValue& v : all[QLatin1String("drags")].toArray()) {
        const QString key = v[QLatin1String("part")].toString();
        if (key == QLatin1String("IMPORTTEST.1")) continue;
        const auto fp = lib.footprint(key, v[QLatin1String("orientation")].toDouble());
        ASSERT_TRUE(fp) << key.toStdString();
        const QJsonArray area = v[QLatin1String("area")].toArray();
        EXPECT_NEAR(fp->size.width(), area[2].toDouble(), 1e-3) << caseName(v.toObject()).toStdString();
        EXPECT_NEAR(fp->size.height(), area[3].toDouble(), 1e-3) << caseName(v.toObject()).toStdString();
    }
}

namespace {

// "SM.7": a 17 x 8 stud part with half a stud of <SnapMargin> each side,
// like the 9V straight; "TT.7": a 4 x 2 stud track with rail ends.
struct GridLibrary {
    QTemporaryDir dir;
    parts::PartsLibrary lib;
    GridLibrary() {
        QFile f(dir.filePath(QStringLiteral("SM.7.xml")));
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<part><SnapMargin><left>0.5</left><right>0.5</right><top>0</top><bottom>0</bottom></SnapMargin></part>");
        f.close();
        QImage sprite(136, 64, QImage::Format_ARGB32);
        sprite.fill(Qt::darkGray);
        sprite.save(dir.filePath(QStringLiteral("SM.7.png")));
        QFile t(dir.filePath(QStringLiteral("TT.7.xml")));
        EXPECT_TRUE(t.open(QIODevice::WriteOnly));
        t.write("<part><ConnexionList>"
                "<connexion><type>1</type><position><x>-2</x><y>0</y></position><angle>180</angle></connexion>"
                "<connexion><type>1</type><position><x>2</x><y>0</y></position><angle>0</angle></connexion>"
                "</ConnexionList></part>");
        t.close();
        QImage track(32, 16, QImage::Format_ARGB32);
        track.fill(Qt::darkGray);
        track.save(dir.filePath(QStringLiteral("TT.7.png")));
        lib.addSearchPath(dir.path());
        lib.scan();
    }
};

core::Brick part(const QString& guid, const QString& key, QRectF area) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = key;
    b.displayArea = area;
    return b;
}

// A map with one SM part at (3.3, 2.2), a ruler from (1.2, 1.1) to
// (9.2, 1.1) and a label at (10.3, 30.6), on an 8-stud grid; shown at 14
// screen px a stud.
class GridSnapView : public ::testing::Test {
protected:
    void SetUp() override {
        before_ = theme::PrefsStore::instance().prefs();
        auto map = std::make_unique<core::Map>();
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = QStringLiteral("L");
        layer->name = QStringLiteral("Parts");
        layer->bricks.push_back(part(QStringLiteral("S"), QStringLiteral("SM.7"), QRectF(3.3, 2.2, 17, 8)));
        map->layers().push_back(std::move(layer));
        auto rulers = std::make_unique<core::LayerRuler>();
        rulers->guid = QStringLiteral("R");
        core::LayerRuler::AnyRuler r;
        r.kind = core::RulerKind::Linear;
        r.linear.guid = QStringLiteral("ruler");
        r.linear.point1 = QPointF(1.2, 41.1);
        r.linear.point2 = QPointF(9.2, 41.1);
        r.linear.displayArea = QRectF(1.2, 41.1, 8, 0);
        rulers->rulers.push_back(r);
        map->layers().push_back(std::move(rulers));
        core::AnchoredLabel label;
        label.id = QStringLiteral("label");
        label.text = QStringLiteral("Station");
        label.font.sizePt = 12;
        label.offset = QPointF(10.3, 30.6);
        map->sidecar.anchoredLabels.push_back(label);
        view_ = std::make_unique<MapView>(lib_.lib);
        view_->resize(1000, 900);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(map));
        view_->setSnapStepStuds(8.0);
        view_->setTransform(QTransform::fromScale(kStudPx / kPx, kStudPx / kPx));
        view_->centerOn(QPointF(20 * kPx, 25 * kPx));
    }
    void TearDown() override { theme::PrefsStore::instance().update(before_); }

    // A slow drag by the pointer from `from` to `to` (studs).
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers mods = {}, bool release = true) {
        QWidget* vp = view_->viewport();
        QTest::mousePress(vp, Qt::LeftButton, mods, screen(from));
        const int steps = 12;
        for (int i = 1; i <= steps; ++i) {
            const QPoint p = screen(from + (to - from) * i / steps);
            QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, mods);
            QApplication::sendEvent(vp, &move);
        }
        if (release) QTest::mouseRelease(vp, Qt::LeftButton, mods, screen(to));
    }
    void click(QPointF at, Qt::KeyboardModifiers mods = {}) {
        QTest::mouseClick(view_->viewport(), Qt::LeftButton, mods, screen(at));
    }
    // Where the pointer really is for `studs` (whole screen pixels).
    QPointF onScreen(QPointF studs) const { return view_->mapToScene(screen(studs)) / kPx; }
    QPoint screen(QPointF studs) const { return view_->mapFromScene(studs * kPx); }

    const core::Brick* brick(const QString& guid) {
        for (const auto& l : view_->currentMap()->layers())
            if (l->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                    if (b.guid == guid) return &b;
        return nullptr;
    }
    const core::LinearRuler* ruler(const QString& guid) {
        for (const auto& l : view_->currentMap()->layers())
            if (l->kind() == core::LayerKind::Ruler)
                for (const auto& r : static_cast<const core::LayerRuler&>(*l).rulers)
                    if (r.kind == core::RulerKind::Linear && r.linear.guid == guid) return &r.linear;
        return nullptr;
    }
    const core::LinearRuler* lastRuler() {
        const core::LinearRuler* out = nullptr;
        for (const auto& l : view_->currentMap()->layers())
            if (l->kind() == core::LayerKind::Ruler)
                for (const auto& r : static_cast<const core::LayerRuler&>(*l).rulers)
                    if (r.kind == core::RulerKind::Linear) out = &r.linear;
        return out;
    }
    QPointF labelAt() const { return view_->currentMap()->sidecar.anchoredLabels.front().offset; }
    // Picks the items of `kind` with id `guid`, only.
    void pick(const QString& kind, const QString& guid) {
        for (QGraphicsItem* it : view_->scene()->items())
            it->setSelected(it->data(detail::kBrickDataKind).toString() == kind
                            && it->data(detail::kBrickDataGuid).toString() == guid);
    }
    QGraphicsItem* item(const QString& kind, const QString& guid) {
        for (QGraphicsItem* it : view_->scene()->items())
            if (it->data(detail::kBrickDataKind).toString() == kind
                && it->data(detail::kBrickDataGuid).toString() == guid)
                return it;
        return nullptr;
    }

    static constexpr double kStudPx = 14.0;
    static constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;
    GridLibrary lib_;
    std::unique_ptr<MapView> view_;
    theme::AppPrefs before_;
};

}  // namespace

// Grabbed at (10, 5), 6.2 by 2.8 studs from its snap corner (3.8, 2.2):
// dragged to (27.6, 13.9), the corner goes to the grid line at or before
// the pointer, less the whole steps it had from it (none): (24, 8). The box
// is half a stud left of it, so the 16-stud body sits on the grid.
TEST_F(GridSnapView, APartLandsByItsSnapCorner) {
    pick(QStringLiteral("brick"), QStringLiteral("S"));
    const QPointF from = onScreen({ 10, 5 });
    const QPointF to = onScreen({ 27.6, 13.9 });
    drag(from, to, {}, false);
    // Live, before the drop: already there.
    QGraphicsItem* it = item(QStringLiteral("brick"), QStringLiteral("S"));
    ASSERT_TRUE(it);
    EXPECT_NEAR(it->scenePos().x() / kPx, 23.5 + 8.5, 1e-6);
    EXPECT_NEAR(it->scenePos().y() / kPx, 8 + 4, 1e-6);
    QTest::mouseRelease(view_->viewport(), Qt::LeftButton, {}, screen(to));
    ASSERT_TRUE(brick(QStringLiteral("S")));
    EXPECT_NEAR(brick(QStringLiteral("S"))->displayArea.x(), 23.5, 1e-6);
    EXPECT_NEAR(brick(QStringLiteral("S"))->displayArea.y(), 8, 1e-6);
}

TEST_F(GridSnapView, APartGoesWhereDraggedWithTheGridOff) {
    view_->setSnapStepStuds(0);
    pick(QStringLiteral("brick"), QStringLiteral("S"));
    const QPointF from = onScreen({ 10, 5 });
    const QPointF to = onScreen({ 27.6, 13.9 });
    drag(from, to);
    EXPECT_NEAR(brick(QStringLiteral("S"))->displayArea.x(), 3.3 + (to - from).x(), 1e-6);
    EXPECT_NEAR(brick(QStringLiteral("S"))->displayArea.y(), 2.2 + (to - from).y(), 1e-6);
}

// Alt only lets go of connections: a part keeps to the grid.
TEST_F(GridSnapView, AltKeepsAPartOnTheGrid) {
    pick(QStringLiteral("brick"), QStringLiteral("S"));
    drag(onScreen({ 10, 5 }), onScreen({ 27.6, 13.9 }), Qt::AltModifier);
    EXPECT_NEAR(brick(QStringLiteral("S"))->displayArea.x(), 23.5, 1e-6);
}

// From the parts list, held by the middle of its 17 x 8 box: its corner is
// 8 by 4 studs from the pointer, so at (20.2, 9.7) it lands at (16 - 8, 8 - 0).
TEST_F(GridSnapView, ANewPartLandsByItsSnapCorner) {
    QPointF centre;
    float orientation = 0;
    bool snapped = false;
    view_->resolvePartPlacement(QStringLiteral("SM.7"), QPointF(20.2, 9.7) * kPx, &centre, &orientation, &snapped,
                                nullptr);
    EXPECT_FALSE(snapped);
    EXPECT_NEAR(centre.x(), 7.5 + 8.5, 1e-6);
    EXPECT_NEAR(centre.y(), 8 + 4, 1e-6);
}

// A module dropped on the grid lands by its part with a connection.
TEST_F(GridSnapView, AModuleLandsByItsLeadPart) {
    core::Map module;
    auto layer = std::make_unique<core::LayerBrick>();
    layer->name = QStringLiteral("Parts");
    layer->bricks.push_back(part(QStringLiteral("m1"), QStringLiteral("SM.7"), QRectF(0.3, 0.3, 17, 8)));
    layer->bricks.push_back(part(QStringLiteral("m2"), QStringLiteral("TT.7"), QRectF(20.1, 1.7, 4, 2)));
    module.layers().push_back(std::move(layer));
    ASSERT_TRUE(view_->placeModule(module, QStringLiteral("M"), {}, QPointF(61.3, 70.9) * kPx));
    const core::Brick* track = nullptr;
    const core::Brick* plate = nullptr;
    for (const auto& l : view_->currentMap()->layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) {
                if (b.partNumber == QLatin1String("TT.7")) track = &b;
                if (b.partNumber == QLatin1String("SM.7") && b.guid != QLatin1String("S")) plate = &b;
            }
    ASSERT_TRUE(track && plate);
    EXPECT_NEAR(std::remainder(track->displayArea.x(), 8.0), 0, 1e-6);
    EXPECT_NEAR(std::remainder(track->displayArea.y(), 8.0), 0, 1e-6);
    // The rest keep their places around it.
    EXPECT_NEAR(track->displayArea.x() - plate->displayArea.x(), 19.8, 1e-6);
    EXPECT_NEAR(track->displayArea.y() - plate->displayArea.y(), 1.4, 1e-6);
}

TEST_F(GridSnapView, ARulerIsDrawnOnTheGrid) {
    view_->setTool(MapView::Tool::DrawLinearRuler);
    drag({ 3.3, 50.4 }, { 21.9, 60.1 });
    const core::LinearRuler* r = lastRuler();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->point1, QPointF(0, 48));
    EXPECT_EQ(r->point2, QPointF(24, 64));
}

TEST_F(GridSnapView, AltDrawsARulerFreely) {
    view_->setTool(MapView::Tool::DrawLinearRuler);
    const QPointF from = onScreen({ 3.3, 50.4 });
    const QPointF to = onScreen({ 21.9, 60.1 });
    drag(from, to, Qt::AltModifier);
    const core::LinearRuler* r = lastRuler();
    ASSERT_TRUE(r);
    EXPECT_NEAR(r->point1.x(), from.x(), 1e-6);
    EXPECT_NEAR(r->point2.y(), to.y(), 1e-6);
}

// The ruler's first end goes to the nearest grid point; the rest follows.
TEST_F(GridSnapView, AMovedRulerLandsOnTheGrid) {
    pick(QStringLiteral("ruler"), QStringLiteral("ruler"));
    drag({ 5.2, 41.1 }, { 12.5, 47.7 });
    const core::LinearRuler* r = ruler(QStringLiteral("ruler"));
    ASSERT_TRUE(r);
    EXPECT_NEAR(r->point1.x(), 8, 1e-6);
    EXPECT_NEAR(r->point1.y(), 48, 1e-6);
    EXPECT_NEAR(r->point2.x(), 16, 1e-6);
    EXPECT_NEAR(r->point2.y(), 48, 1e-6);
}

TEST_F(GridSnapView, AStretchedRulerEndLandsOnTheGrid) {
    pick(QStringLiteral("ruler"), QStringLiteral("ruler"));
    drag({ 9.2, 41.1 }, { 19.1, 44.2 });
    const core::LinearRuler* r = ruler(QStringLiteral("ruler"));
    ASSERT_TRUE(r);
    EXPECT_EQ(r->point2, QPointF(16, 48));
    EXPECT_NEAR(r->point1.x(), 1.2, 1e-6);  // the other end stays
}

TEST_F(GridSnapView, AMovedLabelLandsOnTheGrid) {
    pick(QStringLiteral("label"), QStringLiteral("label"));
    QGraphicsItem* it = item(QStringLiteral("label"), QStringLiteral("label"));
    ASSERT_TRUE(it);
    const QPointF grab = it->sceneBoundingRect().center() / kPx;
    drag(grab, grab + QPointF(5, -3));
    EXPECT_NEAR(labelAt().x(), 16, 1e-6);
    EXPECT_NEAR(labelAt().y(), 24, 1e-6);
}

TEST_F(GridSnapView, AltMovesALabelFreely) {
    pick(QStringLiteral("label"), QStringLiteral("label"));
    QGraphicsItem* it = item(QStringLiteral("label"), QStringLiteral("label"));
    ASSERT_TRUE(it);
    const QPointF grab = onScreen(it->sceneBoundingRect().center() / kPx);
    const QPointF to = onScreen(grab + QPointF(5, -3));
    drag(grab, to, Qt::AltModifier);
    EXPECT_NEAR(labelAt().x(), 10.3 + (to - grab).x(), 1e-6);
    EXPECT_NEAR(labelAt().y(), 30.6 + (to - grab).y(), 1e-6);
}

// A part and a label picked together move by the same amount: the part's.
TEST_F(GridSnapView, PickedTogetherTheyMoveAlike) {
    for (QGraphicsItem* it : view_->scene()->items()) {
        const QString kind = it->data(detail::kBrickDataKind).toString();
        it->setSelected(kind == QLatin1String("brick") || kind == QLatin1String("label"));
    }
    drag(onScreen({ 10, 5 }), onScreen({ 27.6, 13.9 }));
    const QPointF moved = brick(QStringLiteral("S"))->displayArea.topLeft() - QPointF(3.3, 2.2);
    EXPECT_NEAR(labelAt().x(), 10.3 + moved.x(), 1e-6);
    EXPECT_NEAR(labelAt().y(), 30.6 + moved.y(), 1e-6);
}

TEST_F(GridSnapView, NewTextIsCentredOnTheGrid) {
    view_->addTextAtScenePos(QStringLiteral("Hello"), QPointF(13.1, 5.2) * kPx);
    const core::LayerText* text = nullptr;
    for (const auto& l : view_->currentMap()->layers())
        if (l->kind() == core::LayerKind::Text) text = static_cast<const core::LayerText*>(l.get());
    ASSERT_TRUE(text && !text->textCells.empty());
    EXPECT_NEAR(text->textCells.back().displayArea.center().x(), 16, 1e-6);
    EXPECT_NEAR(text->textCells.back().displayArea.center().y(), 8, 1e-6);
}

TEST_F(GridSnapView, VenueCornersAreDrawnOnTheGrid) {
    view_->setTool(MapView::Tool::DrawVenueOutline);
    click({ 3.1, 3.3 });
    click({ 61.0, 4.6 });
    click({ 58.7, 45.2 });
    QTest::keyClick(view_.get(), Qt::Key_Return);
    const auto& venue = view_->currentMap()->sidecar.venue;
    ASSERT_TRUE(venue && venue->edges.size() == 3);
    EXPECT_EQ(venue->edges[0].polyline.front(), QPointF(0, 0));
    EXPECT_EQ(venue->edges[1].polyline.front(), QPointF(64, 8));
    EXPECT_EQ(venue->edges[2].polyline.front(), QPointF(56, 48));
}

TEST_F(GridSnapView, ArrowKeysMoveByTheGridStep) {
    pick(QStringLiteral("brick"), QStringLiteral("S"));
    QTest::keyClick(view_.get(), Qt::Key_Right);
    EXPECT_NEAR(brick(QStringLiteral("S"))->displayArea.x(), 3.3 + 8, 1e-6);
}
