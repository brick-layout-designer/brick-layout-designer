// Calm connection snapping (ui/SnapFeel.h, ui/ConnectionSnap.h): the cases
// the web runs too (fixtures/snap-vectors.json), then the real drags,
// placements and module drops in a MapView.

#include "ui/ConnectionSnap.h"
#include "ui/MapView.h"
#include "ui/SnapFeel.h"
#include "ui/theme/AppPrefs.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QFile>
#include <QMouseEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace bld;
using namespace bld::ui;

namespace {

QJsonObject vectors() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/snap-vectors.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

std::vector<snapfeel::Candidate> candidates(const QJsonArray& list) {
    std::vector<snapfeel::Candidate> out;
    for (const QJsonValue& v : list) {
        const QJsonObject c = v.toObject();
        out.push_back({ c[QLatin1String("m")].toString(), c[QLatin1String("t")].toString(),
                        c[QLatin1String("d")].toDouble(), c[QLatin1String("c")].toDouble() });
    }
    return out;
}

std::optional<snapfeel::Lock> lockOf(const QJsonValue& v) {
    if (!v.isString()) return std::nullopt;
    const QStringList parts = v.toString().split(QLatin1Char('>'));
    return snapfeel::Lock{ parts.value(0), parts.value(1) };
}

QJsonValue named(const std::vector<snapfeel::Candidate>& cands, int i) {
    if (i < 0) return QJsonValue::Null;
    return cands[i].movingKey + QLatin1Char('>') + cands[i].targetKey;
}

snapfeel::Strength strengthOf(const QJsonValue& v) {
    return snapfeel::strengthFromId(v.toString()).value_or(snapfeel::Strength::Gentle);
}

}  // namespace

TEST(SnapFeel, UsesTheSharedNumbers) {
    const QJsonObject c = vectors()[QLatin1String("constants")].toObject();
    using namespace snapfeel;
    EXPECT_EQ(kReachScreenPx, c[QLatin1String("reachScreenPx")].toDouble());
    EXPECT_EQ(kMinReachStuds, c[QLatin1String("minReachStuds")].toDouble());
    EXPECT_EQ(kMaxReachStuds, c[QLatin1String("maxReachStuds")].toDouble());
    EXPECT_EQ(kHoldFactor, c[QLatin1String("holdFactor")].toDouble());
    EXPECT_EQ(kSwitchRatio, c[QLatin1String("switchRatio")].toDouble());
    EXPECT_EQ(kSwitchMinStuds, c[QLatin1String("switchMinStuds")].toDouble());
    EXPECT_EQ(kTieStuds, c[QLatin1String("tieStuds")].toDouble());
    EXPECT_EQ(kFastPxPerSecond, c[QLatin1String("fastPxPerSecond")].toDouble());
    EXPECT_EQ(kSpeedSamples, c[QLatin1String("speedSamples")].toInt());
    EXPECT_EQ(kSpeedWindowMs, c[QLatin1String("speedWindowMs")].toDouble());
    const QJsonObject s = c[QLatin1String("strength")].toObject();
    EXPECT_EQ(strengthScale(Strength::Off), s[QLatin1String("off")].toDouble());
    EXPECT_EQ(strengthScale(Strength::Gentle), s[QLatin1String("gentle")].toDouble());
    EXPECT_EQ(strengthScale(Strength::Strong), s[QLatin1String("strong")].toDouble());
}

TEST(SnapFeel, SharedReachCases) {
    for (const QJsonValue& v : vectors()[QLatin1String("reach")].toArray()) {
        const QJsonObject c = v.toObject();
        EXPECT_NEAR(snapfeel::reachStuds(c[QLatin1String("pxPerStud")].toDouble(), strengthOf(c[QLatin1String("strength")])),
                    c[QLatin1String("reach")].toDouble(), 1e-9)
            << c[QLatin1String("name")].toString().toStdString();
    }
}

TEST(SnapFeel, SharedPickCases) {
    const QJsonArray cases = vectors()[QLatin1String("pick")].toArray();
    EXPECT_GE(cases.size(), 20);
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const auto cands = candidates(c[QLatin1String("candidates")].toArray());
        const int i = snapfeel::pick(cands, lockOf(c[QLatin1String("lock")]), c[QLatin1String("reach")].toDouble(),
                                     c[QLatin1String("fast")].toBool(), c[QLatin1String("bypass")].toBool());
        EXPECT_EQ(named(cands, i), c[QLatin1String("expect")]) << c[QLatin1String("name")].toString().toStdString();
    }
}

TEST(SnapFeel, SharedSpeedCases) {
    for (const QJsonValue& v : vectors()[QLatin1String("speed")].toArray()) {
        const QJsonObject c = v.toObject();
        snapfeel::SpeedMeter m;
        for (const QJsonValue& s : c[QLatin1String("samples")].toArray()) {
            const QJsonArray a = s.toArray();
            m.sample(a[0].toDouble(), a[1].toDouble(), a[2].toDouble());
        }
        const std::string name = c[QLatin1String("name")].toString().toStdString();
        EXPECT_NEAR(m.speed(), c[QLatin1String("speed")].toDouble(), 1e-6) << name;
        EXPECT_EQ(m.isFast(), c[QLatin1String("fast")].toBool()) << name;
    }
}

TEST(SnapFeel, SharedSessionCases) {
    for (const QJsonValue& v : vectors()[QLatin1String("session")].toArray()) {
        const QJsonObject c = v.toObject();
        const std::string name = c[QLatin1String("name")].toString().toStdString();
        snapfeel::Session s;
        int frame = 0;
        for (const QJsonValue& fv : c[QLatin1String("frames")].toArray()) {
            const QJsonObject f = fv.toObject();
            for (const QJsonValue& sv : f[QLatin1String("samples")].toArray()) {
                const QJsonArray a = sv.toArray();
                s.sample(a[0].toDouble(), a[1].toDouble(), a[2].toDouble());
            }
            const auto cands = candidates(f[QLatin1String("candidates")].toArray());
            const int i = s.step(cands, c[QLatin1String("reach")].toDouble(), f[QLatin1String("bypass")].toBool(),
                                 f[QLatin1String("final")].toBool());
            EXPECT_EQ(named(cands, i), f[QLatin1String("expect")]) << name << " frame " << frame;
            EXPECT_EQ(s.lock.has_value(), i >= 0) << name << " frame " << frame;
            ++frame;
        }
    }
}

TEST(SnapFeel, StrengthIds) {
    using snapfeel::Strength;
    for (Strength s : { Strength::Off, Strength::Gentle, Strength::Strong })
        EXPECT_EQ(snapfeel::strengthFromId(snapfeel::strengthId(s)), s);
    EXPECT_FALSE(snapfeel::strengthFromId(QStringLiteral("medium")));
}

TEST(ConnectionSnap, PairsOnlyMatchingTypesWithinTheHoldDistance) {
    const std::vector<MovingConn> moving = {
        { QStringLiteral("m#0"), QStringLiteral("1"), { 0, 0 }, 0 },
        { QStringLiteral("m#1"), QStringLiteral("2"), { 10, 0 }, 0 },
    };
    const std::vector<FreeTarget> targets = {
        { QStringLiteral("t#0"), QStringLiteral("2"), { 0.2, 0 }, 0 },   // wrong type for m#0
        { QStringLiteral("t#1"), QStringLiteral("1"), { 0.8, 0 }, 0 },
        { QStringLiteral("t#2"), QStringLiteral("2"), { 11.5, 0 }, 0 },  // past the hold for m#1
    };
    const SnapPick p = pickConnectionSnap(moving, targets, 1.0, nullptr);
    ASSERT_TRUE(p.applied());
    EXPECT_EQ(p.moving, 0);
    EXPECT_EQ(p.target, 1);
    // Held, m#1 > t#2 is 1.5 studs off: inside 1.6x a 1-stud reach.
    snapfeel::Session s;
    s.lock = snapfeel::Lock{ QStringLiteral("m#1"), QStringLiteral("t#2") };
    const SnapPick held = pickConnectionSnap({ moving[1] }, targets, 1.0, &s);
    EXPECT_EQ(held.target, 2);
    EXPECT_FALSE(pickConnectionSnap({ moving[1] }, targets, 1.0, nullptr).applied());
    EXPECT_FALSE(pickConnectionSnap(moving, targets, 1.0, nullptr, /*bypass=*/true).applied());
    EXPECT_FALSE(pickConnectionSnap(moving, targets, 0.0, nullptr).applied());
    EXPECT_DOUBLE_EQ(facingOrientation(0, 180), 0);
    EXPECT_DOUBLE_EQ(facingOrientation(90, 0), -90);
    EXPECT_DOUBLE_EQ(facingOrientation(0, 0), 180);
}

namespace {

// "TT.7": a 4 x 2 stud straight with rail connections at its ends.
struct TrackLibrary {
    QTemporaryDir dir;
    parts::PartsLibrary lib;
    TrackLibrary() {
        QFile f(dir.filePath(QStringLiteral("TT.7.xml")));
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<part><ConnexionList>"
                "<connexion><type>1</type><position><x>-2</x><y>0</y></position><angle>180</angle></connexion>"
                "<connexion><type>1</type><position><x>2</x><y>0</y></position><angle>0</angle></connexion>"
                "</ConnexionList></part>");
        f.close();
        QImage sprite(32, 16, QImage::Format_ARGB32);
        sprite.fill(Qt::darkGray);
        sprite.save(dir.filePath(QStringLiteral("TT.7.png")));
        lib.addSearchPath(dir.path());
        lib.scan();
    }
};

core::Brick track(const QString& guid, double x) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = QStringLiteral("TT.7");
    b.displayArea = QRectF(x, 0, 4, 2);
    return b;
}

// A straight A at x 0..4 (right end at (4, 1)) and B at x 10..14 (left
// end at (10, 1)): 6 studs apart. Shown at 14 screen px a stud, where the
// Gentle reach is exactly 1 stud.
class SnapDragTest : public ::testing::Test {
protected:
    void SetUp() override {
        before_ = theme::PrefsStore::instance().prefs();
        setStrength(QStringLiteral("gentle"));
        auto map = std::make_unique<core::Map>();
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = QStringLiteral("L");
        layer->bricks.push_back(track(QStringLiteral("A"), 0));
        layer->bricks.push_back(track(QStringLiteral("B"), 10));
        map->layers().push_back(std::move(layer));
        view_ = std::make_unique<MapView>(lib_.lib);
        view_->resize(900, 400);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(map));
        view_->setSnapStepStuds(0.0);
        view_->setTransform(QTransform::fromScale(kStudPx / kPx, kStudPx / kPx));
        view_->centerOn(QPointF(7 * kPx, 1 * kPx));
    }
    void TearDown() override {
        theme::PrefsStore::instance().update(before_);
    }
    static void setStrength(const QString& id) {
        theme::AppPrefs p = theme::PrefsStore::instance().prefs();
        p.connectionSnap = id;
        theme::PrefsStore::instance().update(p);
    }
    // Start over on a map of just `bricks` (one sheet), linked as they touch.
    void reload(std::vector<core::Brick> bricks, QPointF centre) {
        auto map = std::make_unique<core::Map>();
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = QStringLiteral("L");
        layer->bricks = std::move(bricks);
        map->layers().push_back(std::move(layer));
        view_->loadMap(std::move(map));
        view_->setTransform(QTransform::fromScale(kStudPx / kPx, kStudPx / kPx));
        view_->centerOn(centre * kPx);
    }
    const core::Brick* brick(const QString& guid) const {
        for (const auto& l : view_->currentMap()->layers())
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (b.guid == guid) return &b;
        return nullptr;
    }
    // A slow drag by the pointer from `from` to `to` (studs), then let go.
    void slowDrag(QPointF from, QPointF to, bool release = true, bool fast = false) {
        QWidget* vp = view_->viewport();
        if (!dragging_) QTest::mousePress(vp, Qt::LeftButton, {}, screen(from));
        dragging_ = !release;
        const int steps = std::max(1, static_cast<int>(std::hypot(to.x() - from.x(), to.y() - from.y()) * kStudPx / 8));
        for (int i = 1; i <= steps; ++i) {
            const QPoint p = screen(from + (to - from) * i / steps);
            QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
            QApplication::sendEvent(vp, &move);
            if (!fast) QTest::qWait(20);
        }
        if (release) QTest::mouseRelease(vp, Qt::LeftButton, {}, screen(to));
    }
    bool dragging_ = false;
    QPoint screen(QPointF studs) const { return view_->mapFromScene(studs * kPx); }
    double bx() const {
        for (const auto& l : view_->currentMap()->layers())
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (b.guid == QLatin1String("B")) return b.displayArea.x();
        return -1;
    }
    // Drag B by the pointer, through `gaps` (studs between the two ends),
    // slowly (about 400 px/s) unless `fast`.
    void dragB(std::initializer_list<double> gaps, bool fast = false, Qt::KeyboardModifiers mods = {}) {
        const QPointF grab(12, 1);
        QWidget* vp = view_->viewport();
        QTest::mousePress(vp, Qt::LeftButton, mods, screen(grab));
        QPointF at = grab;
        for (double gap : gaps) {
            const QPointF to(12 - (6 - gap), 1);
            const int steps = fast ? 2 : std::max(1, static_cast<int>(std::abs(to.x() - at.x()) * kStudPx / 8));
            for (int i = 1; i <= steps; ++i) {
                const QPoint p = screen(at + (to - at) * i / steps);
                QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, mods);
                QApplication::sendEvent(vp, &move);
                if (!fast) QTest::qWait(20);
            }
            at = to;
        }
        QTest::mouseRelease(vp, Qt::LeftButton, mods, screen(at));
    }
    static constexpr double kStudPx = 14.0;
    static constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;
    TrackLibrary lib_;
    std::unique_ptr<MapView> view_;
    theme::AppPrefs before_;
};

}  // namespace

TEST_F(SnapDragTest, ReachIsOnScreenAndKeptWithinLimits) {
    EXPECT_NEAR(view_->connectionSnapReachStuds(), 1.0, 1e-9);
    view_->setTransform(QTransform::fromScale(1, 1));  // 8 px a stud
    EXPECT_NEAR(view_->connectionSnapReachStuds(), 1.75, 1e-9);
    view_->setTransform(QTransform::fromScale(0.25, 0.25));
    EXPECT_NEAR(view_->connectionSnapReachStuds(), 4.0, 1e-9);
    view_->setTransform(QTransform::fromScale(10, 10));
    EXPECT_NEAR(view_->connectionSnapReachStuds(), 0.5, 1e-9);
    setStrength(QStringLiteral("strong"));
    EXPECT_NEAR(view_->connectionSnapReachStuds(), 0.8, 1e-9);
    setStrength(QStringLiteral("off"));
    EXPECT_EQ(view_->connectionSnapReachStuds(), 0.0);
}

TEST_F(SnapDragTest, SnapsWithinReachAndHoldsInside1Point6x) {
    dragB({ 0.5, 1.4 });
    EXPECT_NEAR(bx(), 4.0, 1e-6);  // still joined to A's end
}

TEST_F(SnapDragTest, LetsGoPast1Point6xTheReach) {
    dragB({ 0.5, 1.8 });
    EXPECT_NEAR(bx(), 5.8, 0.1);
}

TEST_F(SnapDragTest, OutsideTheReachNoSnap) {
    dragB({ 1.3 });
    EXPECT_NEAR(bx(), 5.3, 0.1);
}

TEST_F(SnapDragTest, AFastDragSnapsOnTheDrop) {
    dragB({ 0.5 }, /*fast=*/true);
    EXPECT_NEAR(bx(), 4.0, 1e-6);
}

TEST_F(SnapDragTest, AltPlacesWithoutSnapping) {
    dragB({ 0.5 }, false, Qt::AltModifier);
    EXPECT_NEAR(bx(), 4.5, 0.1);
}

TEST_F(SnapDragTest, OffNeverSnaps) {
    setStrength(QStringLiteral("off"));
    dragB({ 0.3 });
    EXPECT_NEAR(bx(), 4.3, 0.1);
}

TEST_F(SnapDragTest, StrongReachesFurther) {
    setStrength(QStringLiteral("strong"));
    dragB({ 1.4 });
    EXPECT_NEAR(bx(), 4.0, 1e-6);
}

TEST_F(SnapDragTest, ANewPartSnapsWithinTheReach) {
    // Its left end 0.6 studs from A's right end: placed against it.
    view_->addPartAtScenePos(QStringLiteral("TT.7"), QPointF(6.6, 1) * kPx);
    view_->scene()->clearSelection();  // no anchoring on the part just placed
    // Its left end 1.3 studs off: placed where it was put.
    view_->addPartAtScenePos(QStringLiteral("TT.7"), QPointF(7.3, 30) * kPx);
    std::vector<QRectF> added;
    for (const auto& l : view_->currentMap()->layers())
        for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
            if (b.guid != QLatin1String("A") && b.guid != QLatin1String("B")) added.push_back(b.displayArea);
    ASSERT_EQ(added.size(), 2u);
    EXPECT_NEAR(added[0].x(), 4.0, 1e-6);
    EXPECT_NEAR(added[1].x(), 5.3, 1e-6);
}

TEST_F(SnapDragTest, AModuleSnapsWithinTheReach) {
    core::Map module;
    auto layer = std::make_unique<core::LayerBrick>();
    layer->bricks.push_back(track(QString(), 0));
    module.layers().push_back(std::move(layer));
    // Centre at x 6.7: its left end is 0.7 studs from A's right end.
    ASSERT_TRUE(view_->placeModule(module, QStringLiteral("M"), QString(), QPointF(6.7, 1) * kPx));
    double x = -1;
    for (const auto& l : view_->currentMap()->layers())
        for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
            if (b.guid != QLatin1String("A") && b.guid != QLatin1String("B")) x = b.displayArea.x();
    EXPECT_NEAR(x, 4.0, 1e-6);
}

TEST_F(SnapDragTest, ALinkedPartSnapsElsewhereWithoutLettingGo) {
    // A (0..4) and B (4..8) are joined; C (14..18) has a free right end at
    // (18, 1). Grabbing B by its joined left end and pulling that end to
    // C's right end snaps it there in one drag.
    reload({ track(QStringLiteral("A"), 0), track(QStringLiteral("B"), 4), track(QStringLiteral("C"), 14) }, { 12, 1 });
    // Links name the partner connection.
    const auto joined = [this](const QString& a, int i, const QString& b, int j) {
        return brick(a)->connections.at(i).linkedToId == brick(b)->connections.at(j).guid;
    };
    ASSERT_TRUE(joined(QStringLiteral("B"), 0, QStringLiteral("A"), 1));
    slowDrag({ 4.5, 1 }, { 18.9, 1 }, /*release=*/false);  // B's left end 0.4 studs past C's
    EXPECT_TRUE(view_->connectionSnapShown());  // while still dragging
    slowDrag({ 18.9, 1 }, { 18.9, 1 });
    EXPECT_NEAR(brick(QStringLiteral("B"))->displayArea.x(), 18.0, 1e-6);
    // On the drop it lets go of A and joins C.
    EXPECT_TRUE(joined(QStringLiteral("B"), 0, QStringLiteral("C"), 1));
    EXPECT_TRUE(joined(QStringLiteral("C"), 1, QStringLiteral("B"), 0));
    EXPECT_TRUE(brick(QStringLiteral("A"))->connections.at(1).linkedToId.isEmpty());
}

TEST_F(SnapDragTest, ALinkedPartFlungElsewhereSnapsOnTheDrop) {
    // Too fast to snap on the way; the drop's snap still takes the joined end.
    reload({ track(QStringLiteral("A"), 0), track(QStringLiteral("B"), 4), track(QStringLiteral("C"), 14) }, { 12, 1 });
    slowDrag({ 4.5, 1 }, { 18.9, 1 }, /*release=*/true, /*fast=*/true);
    EXPECT_NEAR(brick(QStringLiteral("B"))->displayArea.x(), 18.0, 1e-6);
}

TEST_F(SnapDragTest, AnEndLeftBehindIsFreeForThePartThatLeft) {
    // B is joined to A. Pulled away and brought back in the same drag, it
    // snaps onto the end of A it just left.
    reload({ track(QStringLiteral("A"), 0), track(QStringLiteral("B"), 4) }, { 4, 1 });
    slowDrag({ 6, 1 }, { 6, 8 }, /*release=*/false);
    slowDrag({ 6, 8 }, { 6, 1.3 });  // B's left end 0.3 studs off A's right end
    EXPECT_NEAR(brick(QStringLiteral("B"))->displayArea.y(), 0.0, 1e-6);
    EXPECT_NEAR(brick(QStringLiteral("B"))->displayArea.x(), 4.0, 1e-6);
}

TEST(ConnectionSnap, LinksToTheMovingSetDontHoldWhileDragging) {
    TrackLibrary lib;
    core::Map map;
    auto layer = std::make_unique<core::LayerBrick>();
    core::Brick a = track(QStringLiteral("A"), 0);
    core::Brick b = track(QStringLiteral("B"), 4);
    a.connections.resize(2);
    b.connections.resize(2);
    a.connections[0].guid = QStringLiteral("a0");
    a.connections[1].guid = QStringLiteral("a1");
    b.connections[0].guid = QStringLiteral("b0");
    b.connections[1].guid = QStringLiteral("b1");
    a.connections[1].linkedToId = QStringLiteral("b0");  // the partner connection
    b.connections[0].linkedToId = QStringLiteral("A");   // the other flavour: the partner brick
    layer->bricks = { a, b };
    map.layers().push_back(std::move(layer));

    const auto keys = [&](const QSet<QString>& guids) {
        QStringList out;
        for (const FreeTarget& t : freeTargets(map, lib.lib, guids)) out << t.key;
        out.sort();
        return out;
    };
    // Nothing moving: the joined ends are taken.
    EXPECT_EQ(keys({}), (QStringList{ QStringLiteral("A#0"), QStringLiteral("B#1") }));
    // B moving: A's end that held it is free again (either flavour).
    EXPECT_EQ(keys({ QStringLiteral("B") }), (QStringList{ QStringLiteral("A#0"), QStringLiteral("A#1") }));
    EXPECT_EQ(keys({ QStringLiteral("A") }), (QStringList{ QStringLiteral("B#0"), QStringLiteral("B#1") }));

    const QSet<QString> movingB = linkKeys(map, { QStringLiteral("B") });
    EXPECT_TRUE(movingB.contains(QStringLiteral("b0")));
    EXPECT_FALSE(takenWhileMoving(QStringLiteral("A"), movingB));  // linked to a part left behind
    EXPECT_TRUE(takenWhileMoving(QStringLiteral("b1"), movingB));  // linked inside the moving set
    EXPECT_FALSE(takenWhileMoving(QString(), movingB));
    EXPECT_TRUE(takenWhileStill(QStringLiteral("x"), movingB));
    EXPECT_FALSE(takenWhileStill(QStringLiteral("b0"), movingB));
    // Both moving: their joint stays joined.
    const QSet<QString> both = linkKeys(map, { QStringLiteral("A"), QStringLiteral("B") });
    EXPECT_TRUE(takenWhileMoving(QStringLiteral("b0"), both));
    EXPECT_TRUE(takenWhileMoving(QStringLiteral("A"), both));
}
