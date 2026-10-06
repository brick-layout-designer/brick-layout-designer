// The electric overlay's path through a part (CircuitPath.h). The web
// checks the same table (apps/web/src/editor/test/circuitPath.test.ts reads
// a copy of fixtures/electric/circuit-paths.json), so both apps draw
// identical rails. BLD_WRITE_CIRCUIT_FIXTURE=1 rewrites the table.

#include "rendering/CircuitPath.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

using namespace bld::rendering;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Case {
    const char* name;
    QPointF p1; double a1;
    QPointF p2; double a2;
};

QPointF rot(QPointF v, double deg) {
    const double r = deg * kPi / 180.0;
    return { v.x() * std::cos(r) - v.y() * std::sin(r), v.x() * std::sin(r) + v.y() * std::cos(r) };
}

std::vector<Case> cases() {
    // Connection positions and angles from BlueBrickParts (studs, degrees).
    const QPointF c1(-8.1875, -1.375), c2(7.1198, 1.6698);  // 2867.8, R40 curve
    const QPointF shift(100.5, -20.25);
    return {
        { "straight 2865", { -8, 0 }, 180, { 8, 0 }, 0 },
        { "curve 2867", c1, 180, c2, 22.5 },
        { "curve 2867 backwards", c2, 22.5, c1, 180 },
        { "curve 2867 turned 37 and moved", rot(c1, 37) + shift, 217, rot(c2, 37) + shift, 59.5 },
        { "switch 2861 branch", { -16.9375, 6.375 }, 180, { 15.7552, -6.58018 }, -22.5 },
        { "crossover 7996 diagonal", { -24, -8 }, 180, { 24, 8 }, 0 },
    };
}

QJsonArray toJson(const std::vector<CircuitPathPoint>& path) {
    QJsonArray out;
    for (const auto& pt : path)
        out.append(QJsonArray{ pt.p.x(), pt.p.y(), pt.normal.x(), pt.normal.y(), pt.s });
    return out;
}

const QString kFixture = QStringLiteral(BLD_CIRCUIT_FIXTURE);

}  // namespace

TEST(CircuitPathTest, AStraightIsTheChordWithBlueBricksNormal) {
    const auto path = circuitPath({ -8, 0 }, 180, { 8, 0 }, 0);
    ASSERT_GE(path.size(), 2u);
    for (const auto& pt : path) {
        EXPECT_NEAR(pt.p.y(), 0.0, 1e-9);
        EXPECT_NEAR(pt.normal.x(), 0.0, 1e-9);
        EXPECT_NEAR(pt.normal.y(), 1.0, 1e-9);  // BlueBrick: (-dir.y, dir.x)
    }
    EXPECT_NEAR(path.back().s, 16.0, 1e-9);
}

TEST(CircuitPathTest, ACurveIsOneArcOfItsRadius) {
    for (bool backwards : { false, true }) {
        const QPointF c1(-8.1875, -1.375), c2(7.1198, 1.6698);
        const auto path = backwards ? circuitPath(c2, 22.5, c1, 180) : circuitPath(c1, 180, c2, 22.5);
        ASSERT_GT(path.size(), 5u);
        // The R40 curve's centre: 40 studs along the start normal.
        const QPointF centre = c1 + QPointF(0, 40.0);
        for (const auto& pt : path) {
            const QPointF d = pt.p - centre;
            EXPECT_NEAR(std::hypot(d.x(), d.y()), 40.0, 0.01) << "backwards " << backwards;
        }
        // The rails are concentric: 37.5 and 42.5.
        for (const QPointF& p : offsetPath(path, backwards ? -kCircuitRailOffset : kCircuitRailOffset)) {
            const QPointF d = p - centre;
            EXPECT_NEAR(std::hypot(d.x(), d.y()), 37.5, 0.01);
        }
    }
}

TEST(CircuitPathTest, EndsLeaveAndReachTheConnectionsAlongTheirAngles) {
    for (const Case& c : cases()) {
        const auto path = circuitPath(c.p1, c.a1, c.p2, c.a2);
        ASSERT_GE(path.size(), 2u) << c.name;
        EXPECT_NEAR(path.front().p.x(), c.p1.x(), 1e-9) << c.name;
        EXPECT_NEAR(path.back().p.y(), c.p2.y(), 1e-9) << c.name;
        // Normal = left of the travel direction.
        const QPointF n1 = rot({ 0, 1 }, c.a1 + 180), n2 = rot({ 0, 1 }, c.a2);
        EXPECT_NEAR(path.front().normal.x(), n1.x(), 1e-9) << c.name;
        EXPECT_NEAR(path.front().normal.y(), n1.y(), 1e-9) << c.name;
        EXPECT_NEAR(path.back().normal.x(), n2.x(), 1e-9) << c.name;
        EXPECT_NEAR(path.back().normal.y(), n2.y(), 1e-9) << c.name;
        // No kinks: consecutive normals turn 2 degrees at most.
        for (size_t i = 1; i < path.size(); ++i) {
            const double dotN = path[i].normal.x() * path[i - 1].normal.x() + path[i].normal.y() * path[i - 1].normal.y();
            EXPECT_GT(dotN, std::cos(2.01 * kPi / 180.0)) << c.name << " at " << i;
        }
    }
}

TEST(CircuitPathTest, TheCutterGapIsCutFromTheSecondRail) {
    const auto path = circuitPath({ -8, 0 }, 180, { 8, 0 }, 0);
    const auto first = offsetPathBetween(path, -kCircuitRailOffset, 0.0, kCircuitCutterGap);
    ASSERT_GE(first.size(), 2u);
    EXPECT_NEAR(first.front().x(), -8.0, 1e-9);
    EXPECT_NEAR(first.back().x(), -8.0 + 5.625, 1e-9);
    EXPECT_NEAR(first.back().y(), -2.5, 1e-9);
}

TEST(CircuitPathTest, MatchesTheTableTheWebChecks) {
    QJsonObject table;
    for (const Case& c : cases()) table.insert(QString::fromLatin1(c.name), toJson(circuitPath(c.p1, c.a1, c.p2, c.a2)));
    if (qEnvironmentVariableIsSet("BLD_WRITE_CIRCUIT_FIXTURE")) {
        QFile f(kFixture);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(table).toJson(QJsonDocument::Indented));
        return;
    }
    QFile f(kFixture);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << kFixture.toStdString();
    const QJsonObject expected = QJsonDocument::fromJson(f.readAll()).object();
    ASSERT_EQ(expected.keys(), table.keys());
    for (const QString& key : table.keys()) {
        const QJsonArray got = table.value(key).toArray(), want = expected.value(key).toArray();
        ASSERT_EQ(got.size(), want.size()) << key.toStdString();
        for (int i = 0; i < got.size(); ++i)
            for (int k = 0; k < 5; ++k)
                EXPECT_NEAR(got[i].toArray()[k].toDouble(), want[i].toArray()[k].toDouble(), 1e-9) << key.toStdString() << " " << i;
    }
}
