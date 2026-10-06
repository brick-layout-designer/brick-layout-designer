#include "FlexMove.h"

#include "../core/LayerBrick.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace bld::edit {

float connectionHingeAngle(const QString& type) {
    if (type == QLatin1String("flexpivot"))         return 10.0f;
    if (type == QLatin1String("magnet"))            return 37.0f;
    if (type == QLatin1String("threequarterhinge")) return 90.0f;
    if (type == QLatin1String("halfhinge"))         return 45.0f;
    return 0.0f;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

// A connection point of a brick, or (brick == nullptr) a fixed point where
// a chain ends in a free connection.
struct Conn {
    core::Brick* brick = nullptr;
    int index = -1;
    QPointF fixed;
    bool operator==(const Conn& o) const { return brick == o.brick && index == o.index; }
    bool isNull() const { return !brick && index < 0; }
};

// IKSolver.Bone_2D_CCD. World coordinates have y up (BlueBrick's are y down).
struct Bone {
    double localAngle = 0.0;
    double maxAngle = 2 * kPi;
    double worldX = 0.0, worldY = 0.0;
    Conn conn;  // null for a bone at a brick centre
};

enum class Ccd { Success, Processing, Failure };

double simplifyAngle(double angle) {
    angle = std::fmod(angle, 2 * kPi);
    if (angle < -kPi) angle += 2 * kPi;
    else if (angle > kPi) angle -= 2 * kPi;
    return angle;
}

// IKSolver.CalcIK_2D_CCD (Ryan Juckett's CCD, with BlueBrick's per-bone limit).
Ccd solveCcd(std::vector<Bone>& bones, double targetX, double targetY, double arrivalDist, int numBones) {
    constexpr double epsilon = 0.0001;
    constexpr double trivialArcLength = 0.00001;
    if (numBones < 2) return Ccd::Failure;
    const double arrivalDistSqr = arrivalDist * arrivalDist;
    double endX = bones[numBones - 1].worldX;
    double endY = bones[numBones - 1].worldY;
    bool modifiedBones = false;
    for (int i = numBones - 2; i >= 0; --i) {
        Bone& bone = bones[i];
        const double curToEndX = endX - bone.worldX;
        const double curToEndY = endY - bone.worldY;
        const double curToEndMag = std::sqrt(curToEndX * curToEndX + curToEndY * curToEndY);
        const double curToTargetX = targetX - bone.worldX;
        const double curToTargetY = targetY - bone.worldY;
        const double curToTargetMag = std::sqrt(curToTargetX * curToTargetX + curToTargetY * curToTargetY);
        double cosRot, sinRot;
        const double endTargetMag = curToEndMag * curToTargetMag;
        if (endTargetMag <= epsilon) {
            cosRot = 1;
            sinRot = 0;
        } else {
            cosRot = (curToEndX * curToTargetX + curToEndY * curToTargetY) / endTargetMag;
            sinRot = (curToEndX * curToTargetY - curToEndY * curToTargetX) / endTargetMag;
        }
        double rot = std::acos(std::max(-1.0, std::min(1.0, cosRot)));
        if (sinRot < 0.0) rot = -rot;
        double newLocal = simplifyAngle(bone.localAngle + rot);
        bool recompute = false;
        if (newLocal > bone.maxAngle) {
            rot -= newLocal - bone.maxAngle;
            newLocal = bone.maxAngle;
            recompute = true;
        } else if (newLocal < -bone.maxAngle) {
            rot -= newLocal + bone.maxAngle;
            newLocal = -bone.maxAngle;
            recompute = true;
        }
        bone.localAngle = newLocal;
        if (recompute) {
            cosRot = std::cos(rot);
            sinRot = std::sin(rot);
        }
        endX = bone.worldX + cosRot * curToEndX - sinRot * curToEndY;
        endY = bone.worldY + sinRot * curToEndX + cosRot * curToEndY;
        const double dx = targetX - endX, dy = targetY - endY;
        if (dx * dx + dy * dy <= arrivalDistSqr) return Ccd::Success;
        if (!modifiedBones && std::abs(rot) * curToEndMag > trivialArcLength) modifiedBones = true;
    }
    return modifiedBones ? Ccd::Processing : Ccd::Failure;
}

float simplifyDegrees(float angle) {
    if (angle <= -360.0f) angle += 360.0f;
    if (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

// System.Drawing Matrix.Rotate + TransformVectors.
QPointF rotateVector(QPointF v, double degrees) {
    const double r = degrees * kPi / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { v.x() * c - v.y() * s, v.x() * s + v.y() * c };
}

}  // namespace

struct FlexMove::Impl {
    struct ChainLink {
        Conn first;   // on the next brick (or a fixed point)
        Conn second;  // on this brick
        float angleBetween = 0.0f;
    };

    core::LayerBrick& layer;
    parts::PartsLibrary& lib;
    QHash<const core::Brick*, parts::PartMetadata> meta;
    QHash<QString, Conn> byGuid;  // connection guid -> connection
    std::vector<Bone> bones;
    std::vector<ChainLink> chain;
    std::vector<core::Brick*> chainBricks;
    int rootLink = 0;
    float initialStaticOrientation = 0.0f;
    QPointF lastBoneVector;
    QPointF primaryTarget, secondaryTarget;
    bool useTwoTargets = false;
    Ccd status = Ccd::Failure;
    core::Brick* grabbed = nullptr;
    int active = -1;    // the grabbed brick's connection that follows the mouse
    QPointF grabDelta;  // mouse - active connection, at the start

    Impl(core::LayerBrick& l, parts::PartsLibrary& p) : layer(l), lib(p) {}

    const parts::PartMetadata* metaOf(const core::Brick* b) {
        auto it = meta.find(b);
        if (it == meta.end()) {
            auto m = lib.metadata(b->partNumber);
            it = meta.insert(b, m ? *m : parts::PartMetadata{});
        }
        return &it.value();
    }
    int connectionCount(const core::Brick* b) { return metaOf(b)->connections.size(); }
    QString type(const Conn& c) {
        return c.brick ? metaOf(c.brick)->connections[c.index].type : QString();
    }
    float angle(const Conn& c) {
        return c.brick ? static_cast<float>(metaOf(c.brick)->connections[c.index].angleDegrees) : 0.0f;
    }
    Conn conn(core::Brick* b, int i) { return { b, i, {} }; }
    QPointF world(const Conn& c) {
        if (!c.brick) return c.fixed;
        const auto& cp = metaOf(c.brick)->connections[c.index];
        return parts::placement::imageCentre(*c.brick, lib) + parts::placement::rotated(cp.position, c.brick->orientation);
    }
    // ConnectionPoint.ConnectionLink: the connection this one is linked to.
    Conn link(const Conn& c) {
        if (!c.brick || c.index >= static_cast<int>(c.brick->connections.size())) return {};
        const QString& to = c.brick->connections[c.index].linkedToId;
        return to.isEmpty() ? Conn{} : byGuid.value(to);
    }
    bool isFree(const Conn& c) { return link(c).isNull(); }

    void addBone(const Conn& c, core::Brick* brick, double maxAngleDeg) {
        Bone bone;
        bone.maxAngle = maxAngleDeg * kPi / 180.0;
        const QPointF p = c.isNull() ? brick->displayArea.center() : world(c);
        bone.worldX = p.x();
        bone.worldY = -p.y();
        bone.conn = c;
        bones.insert(bones.begin(), bone);
    }

    void follow(Conn& lead, bool& leadsToFlexible) {
        if (lead.isNull()) return;
        leadsToFlexible |= connectionHingeAngle(type(lead)) != 0.0f;
        if (leadsToFlexible) { lead = {}; return; }
        lead = link(lead);
        if (lead.isNull()) return;
        if (connectionCount(lead.brick) == 2) lead = conn(lead.brick, lead.index == 0 ? 1 : 0);
        else lead = {};
    }

    // FlexMove.findStartingConnectionPoint
    std::optional<Conn> startingConnection(const QSet<core::Brick*>& list, core::Brick* g, QPointF mouse) {
        const Conn first = conn(g, 0), second = conn(g, 1);
        const float firstHinge = connectionHingeAngle(type(first));
        const float secondHinge = connectionHingeAngle(type(second));
        if (firstHinge != 0.0f && secondHinge == 0.0f) return second;
        if (firstHinge == 0.0f && secondHinge != 0.0f) return first;
        if (firstHinge == 0.0f && secondHinge == 0.0f) {
            bool firstFlexible = false, secondFlexible = false;
            Conn firstLead = first, secondLead = second;
            QSet<core::Brick*> remaining = list;
            while ((!firstFlexible || !secondFlexible) && (!firstLead.isNull() || !secondLead.isNull())) {
                core::Brick* firstBrick = firstLead.brick;
                core::Brick* secondBrick = secondLead.brick;
                if (!remaining.contains(firstBrick)) firstLead = {};
                if (!remaining.contains(secondBrick)) secondLead = {};
                remaining.remove(firstBrick);
                remaining.remove(secondBrick);
                follow(firstLead, firstFlexible);
                follow(secondLead, secondFlexible);
            }
            if (firstFlexible && !secondFlexible) return second;
            if (!firstFlexible && secondFlexible) return first;
            if (!firstFlexible && !secondFlexible) return std::nullopt;
        }
        const bool firstNeighbourIn = list.contains(link(first).brick);
        const bool secondNeighbourIn = list.contains(link(second).brick);
        if (firstNeighbourIn && !secondNeighbourIn) return second;
        if (!firstNeighbourIn && secondNeighbourIn) return first;
        const QPointF d1 = world(first) - mouse, d2 = world(second) - mouse;
        return QPointF::dotProduct(d1, d1) < QPointF::dotProduct(d2, d2) ? first : second;
    }

    // FlexMove.ceateFlexChain
    void createChain(const QSet<core::Brick*>& list, core::Brick* g, Conn currentFirst) {
        core::Brick* current = g;
        int hingedLink = -1;  // counted from the chain's end: links are inserted at the front
        addBone(currentFirst, g, 0.0);
        while (current && list.contains(current) && (currentFirst.isNull() || connectionCount(current) == 2)) {
            const int secondIndex = currentFirst == conn(current, 0) ? 1 : 0;
            const Conn currentSecond = conn(current, secondIndex);
            const Conn nextFirst = link(currentSecond);
            core::Brick* next = nextFirst.brick;
            ChainLink l;
            l.second = currentSecond;
            if (!nextFirst.isNull()) {
                l.first = nextFirst;
                float a = angle(currentSecond) + 180.0f - angle(nextFirst);
                if (a >= 360.0f) a -= 360.0f;
                if (a < 0.0f) a += 360.0f;
                l.angleBetween = a;
            } else {
                l.first.fixed = world(currentSecond);
            }
            chain.insert(chain.begin(), l);
            if (hingedLink >= 0) ++hingedLink;
            const float hinge = connectionHingeAngle(type(currentSecond));
            if (hinge != 0.0f) {
                hingedLink = 0;
                addBone(currentSecond, current, hinge);
                float deg = 0.0f;
                if (next) deg = simplifyDegrees(next->orientation - current->orientation - l.angleBetween);
                bones.front().localAngle = deg * kPi / 180.0;
                initialStaticOrientation = next ? -next->orientation : -current->orientation;
            }
            current = next;
            currentFirst = nextFirst;
            if (current == g) break;
        }
        if (hingedLink >= 0) rootLink = hingedLink;

        if (bones.size() > 2) {
            const size_t last = bones.size() - 1;
            lastBoneVector = QPointF(bones[last - 1].worldX - bones[last].worldX,
                                     bones[last - 1].worldY - bones[last].worldY);
            if (!bones[last].conn.isNull())
                lastBoneVector = rotateVector(lastBoneVector, bones[last].conn.brick->orientation);
        }

        const Conn root = chain[rootLink].first;
        if (root.brick) chainBricks.push_back(root.brick);
        for (size_t i = rootLink; i < chain.size(); ++i) chainBricks.push_back(chain[i].second.brick);
    }

    // FlexMove.computeBrickPositionAndOrientation
    void place() {
        size_t boneIndex = 0;
        float flexible = 0.0f;
        float rigid = initialStaticOrientation;
        for (size_t li = rootLink; li < chain.size(); ++li) {
            const ChainLink& l = chain[li];
            const QPointF previous = world(l.first);
            core::Brick* brick = l.second.brick;
            if (boneIndex < bones.size() && l.second == bones[boneIndex].conn) {
                bones[boneIndex].worldX = previous.x();
                bones[boneIndex].worldY = -previous.y();
                flexible += static_cast<float>(bones[boneIndex].localAngle * 180.0 / kPi);
                ++boneIndex;
            }
            rigid += l.angleBetween;
            brick->orientation = -flexible - rigid;
            const auto& cp = metaOf(brick)->connections[l.second.index];
            parts::placement::placeByImageCentre(
                *brick, previous - parts::placement::rotated(cp.position, brick->orientation), lib);
        }
        if (!bones.empty()) {
            Bone& last = bones.back();
            QPointF p;
            if (!last.conn.isNull()) p = world(last.conn);
            else if (bones.size() > 1) p = bones[bones.size() - 2].conn.brick->displayArea.center();
            last.worldX = p.x();
            last.worldY = -p.y();
        }
    }

    // FlexMove.reachTarget + update(), run until the solver settles.
    void reach(QPointF target, const Conn& targetConn) {
        primaryTarget = target;
        useTwoTargets = !targetConn.isNull();
        if (useTwoTargets)
            secondaryTarget = target + rotateVector(lastBoneVector, targetConn.brick->orientation + angle(targetConn) + 180.0);
        status = Ccd::Processing;
        constexpr double kPrecision = 0.1;  // studs
        const int count = static_cast<int>(bones.size());
        for (int step = 0; step < 5000 && status == Ccd::Processing; ++step) {
            if (useTwoTargets) {
                solveCcd(bones, secondaryTarget.x(), -secondaryTarget.y(), kPrecision, count - 1);
                place();
            }
            status = solveCcd(bones, primaryTarget.x(), -primaryTarget.y(), kPrecision, count);
            place();
        }
    }
};

std::vector<FlexEnd> flexRunEnds(const core::LayerBrick& layer, const QSet<QString>& selection,
                                 parts::PartsLibrary& lib, QSet<QString>* runOut) {
    std::vector<FlexEnd> ends;
    QHash<QString, const core::Brick*> byGuid;
    QHash<QString, std::pair<const core::Brick*, int>> owner;  // connection guid -> (brick, index)
    for (const auto& b : layer.bricks) {
        byGuid.insert(b.guid, &b);
        for (int i = 0; i < static_cast<int>(b.connections.size()); ++i)
            if (!b.connections[i].guid.isEmpty()) owner.insert(b.connections[i].guid, { &b, i });
    }
    QHash<const core::Brick*, std::optional<parts::PartMetadata>> metas;
    const auto meta = [&](const core::Brick* b) -> const parts::PartMetadata* {
        auto it = metas.find(b);
        if (it == metas.end()) it = metas.insert(b, lib.metadata(b->partNumber));
        return it.value() ? &*it.value() : nullptr;
    };
    const auto flexible = [&](const core::Brick* b) {
        const auto* m = meta(b);
        if (!m || m->connections.isEmpty() || m->connections.size() > 2) return false;
        return std::any_of(m->connections.cbegin(), m->connections.cend(),
                           [](const auto& c) { return connectionHingeAngle(c.type) != 0.0f; });
    };
    QSet<QString> run;
    std::vector<const core::Brick*> todo;
    for (const QString& guid : selection) {
        const core::Brick* b = byGuid.value(guid);
        if (b && flexible(b) && !run.contains(guid)) { run.insert(guid); todo.push_back(b); }
    }
    while (!todo.empty()) {
        const core::Brick* b = todo.back();
        todo.pop_back();
        for (const auto& c : b->connections) {
            if (c.linkedToId.isEmpty()) continue;
            const core::Brick* next = owner.value(c.linkedToId).first;
            if (!next || run.contains(next->guid) || !flexible(next)) continue;
            run.insert(next->guid);
            todo.push_back(next);
        }
    }
    for (const auto& b : layer.bricks) {
        if (!run.contains(b.guid)) continue;
        const auto* m = meta(&b);
        for (int i = 0; i < m->connections.size() && i < static_cast<int>(b.connections.size()); ++i) {
            if (m->connections[i].type.isEmpty() || !b.connections[i].linkedToId.isEmpty()) continue;
            ends.push_back({ b.guid, i, parts::placement::imageCentre(b, lib) + parts::placement::rotated(m->connections[i].position, b.orientation) });
        }
    }
    if (runOut) *runOut = run;
    return ends;
}

FlexMove::FlexMove(std::unique_ptr<Impl> impl) : d_(std::move(impl)) {}
FlexMove::~FlexMove() = default;

std::unique_ptr<FlexMove> FlexMove::start(core::LayerBrick& layer, const QSet<QString>& selection,
                                          const QString& grabbedGuid, QPointF mouseStuds,
                                          parts::PartsLibrary& lib, int activeConnection) {
    auto d = std::make_unique<Impl>(layer, lib);
    QSet<core::Brick*> list;
    for (auto& b : layer.bricks) {
        const int count = std::min<int>(static_cast<int>(b.connections.size()), d->connectionCount(&b));
        for (int i = 0; i < count; ++i)
            if (!b.connections[i].guid.isEmpty()) d->byGuid.insert(b.connections[i].guid, d->conn(&b, i));
        if (selection.contains(b.guid)) list.insert(&b);
        if (b.guid == grabbedGuid) d->grabbed = &b;
    }
    core::Brick* g = d->grabbed;
    if (!g || !list.contains(g)) return nullptr;
    const auto* m = d->metaOf(g);
    const int n = m->connections.size();
    const bool hasConnection = std::any_of(m->connections.cbegin(), m->connections.cend(),
                                           [](const auto& c) { return !c.type.isEmpty(); });
    if (!hasConnection || n > 2 || static_cast<int>(g->connections.size()) < n) return nullptr;
    Conn startConn;
    if (n == 1) {
        if (connectionHingeAngle(m->connections[0].type) == 0.0f) return nullptr;
    } else {
        const auto s = d->startingConnection(list, g, mouseStuds);
        if (!s) return nullptr;
        startConn = *s;
    }
    d->createChain(list, g, startConn);
    if (d->bones.size() < 2) return nullptr;

    d->active = std::clamp(activeConnection >= 0 ? activeConnection : g->activeConnectionPointIndex, 0, n - 1);
    d->grabDelta = mouseStuds - d->world(d->conn(g, d->active));

    std::unique_ptr<FlexMove> move(new FlexMove(std::move(d)));
    move->initial_ = move->currentState();
    return move;
}

std::optional<QPointF> FlexMove::moveTo(QPointF mouseStuds, double reachStuds, bool snap) {
    // LayerBrick.getMovedSnapPoint for a flex move: the grabbed brick's
    // active connection snaps to the nearest free connection of its type
    // on a brick outside the chain, within `reachStuds` (the editor's
    // connection-snap reach; BlueBrick used max(grid, 4) studs).
    Conn target;
    core::Brick* g = d_->grabbed;
    const Conn active = d_->conn(g, d_->active);
    if (snap && reachStuds > 0.0 && d_->isFree(active)) {
        const QString type = d_->type(active);
        const QPointF virtualPos = mouseStuds - d_->grabDelta;
        double best = reachStuds * reachStuds;
        for (auto& b : d_->layer.bricks) {
            if (std::find(d_->chainBricks.begin(), d_->chainBricks.end(), &b) != d_->chainBricks.end()) continue;
            const auto* m = d_->metaOf(&b);
            for (int i = 0; i < m->connections.size() && i < static_cast<int>(b.connections.size()); ++i) {
                if (m->connections[i].type != type || !b.connections[i].linkedToId.isEmpty()) continue;
                const QPointF delta = d_->world(d_->conn(&b, i)) - virtualPos;
                const double sq = QPointF::dotProduct(delta, delta);
                if (sq < best) { best = sq; target = d_->conn(&b, i); }
            }
        }
    }
    if (!target.isNull()) {
        const QPointF p = d_->world(target);
        d_->reach(p, target);
        return p;
    }
    d_->reach(mouseStuds, {});
    return std::nullopt;
}

std::vector<FlexMove::State> FlexMove::currentState() const {
    std::vector<State> out;
    for (const core::Brick* b : d_->chainBricks) out.push_back({ b->guid, b->orientation, b->displayArea });
    return out;
}

void FlexMove::restore() {
    for (const State& s : initial_) {
        for (core::Brick* b : d_->chainBricks) {
            if (b->guid != s.guid) continue;
            b->orientation = s.orientation;
            b->displayArea = s.area;
        }
    }
}

}  // namespace bld::edit
