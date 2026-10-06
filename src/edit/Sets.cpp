#include "Sets.h"

#include "EditCommands.h"
#include "FlexMove.h"
#include "ModuleCommands.h"

#include "../core/Ids.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <optional>

namespace bld::edit {

namespace {

double normalised(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg > 180.0) deg -= 360.0;
    if (deg <= -180.0) deg += 360.0;
    return deg;
}

double angleGap(double a, double b) { return std::abs(normalised(a - b)); }

void expandInto(parts::PartsLibrary& lib, const parts::PartMetadata& meta, const QString& key, QPointF centre,
                double angle, const QString& parentGroup, ExpandedSet& out, int depth) {
    core::Group group;
    group.guid = core::newBbmId();
    group.partNumber = key.toUpper();
    group.myGroupId = parentGroup;
    out.groups.push_back(group);
    for (const auto& sp : meta.subparts) {
        const QPointF at = centre + parts::placement::rotated(sp.position, angle);
        const double turned = angle + sp.angleDegrees;
        const auto sub = lib.metadata(sp.subKey);
        if (sub && sub->kind == parts::PartKind::Group && depth < 16) {
            expandInto(lib, *sub, sp.subKey, at, turned, group.guid, out, depth + 1);
            continue;
        }
        core::Brick b;
        b.guid = core::newBbmId();
        b.partNumber = sp.subKey.toUpper();
        b.orientation = static_cast<float>(normalised(turned));
        b.myGroupId = group.guid;
        parts::placement::placeByAreaCentre(b, at, lib);
        out.bricks.push_back(std::move(b));
    }
}

const core::LayerBrick* brickLayerAt(const core::Map& map, int i) {
    if (i < 0 || i >= static_cast<int>(map.layers().size())) return nullptr;
    const auto* L = map.layers()[i].get();
    return L && L->kind() == core::LayerKind::Brick ? static_cast<const core::LayerBrick*>(L) : nullptr;
}

bool defaultLook(const core::Module& m) {
    return !m.pinned && m.showName && m.outlineColor.isEmpty() && m.nameColor.isEmpty() && m.sameColor
        && m.sourceFile.isEmpty();
}

// Assign each expanded brick a member, by position (set-relative layout).
std::optional<std::vector<int>> matchByLayout(parts::PartsLibrary& lib, const std::vector<core::Brick>& leaves,
                                              const std::vector<const core::Brick*>& members) {
    // Sprite centres: unlike the box centre, they turn with the part.
    const auto centre = [&](const core::Brick& b) { return parts::placement::imageCentre(b, lib); };
    const auto& first = leaves.front();
    for (const core::Brick* anchor : members) {
        if (anchor->partNumber.compare(first.partNumber, Qt::CaseInsensitive) != 0) continue;
        const double turn = anchor->orientation - first.orientation;
        const QPointF shift = centre(*anchor) - parts::placement::rotated(centre(first), turn);
        std::vector<int> pick(leaves.size(), -1);
        std::vector<bool> used(members.size(), false);
        bool ok = true;
        for (size_t l = 0; l < leaves.size() && ok; ++l) {
            const QPointF want = parts::placement::rotated(centre(leaves[l]), turn) + shift;
            ok = false;
            for (size_t m = 0; m < members.size(); ++m) {
                if (used[m] || members[m]->partNumber.compare(leaves[l].partNumber, Qt::CaseInsensitive) != 0) continue;
                const QPointF d = centre(*members[m]) - want;
                if (std::hypot(d.x(), d.y()) > 0.02) continue;
                if (angleGap(members[m]->orientation, leaves[l].orientation + turn) > 0.1) continue;
                used[m] = true;
                pick[l] = static_cast<int>(m);
                ok = true;
                break;
            }
        }
        if (ok) return pick;
    }
    return std::nullopt;
}

// For a set of distinct parts joined by hinges: each joint of the set's own
// layout is linked between the members, and bent within its hinge angle.
std::optional<std::vector<int>> matchByJoints(parts::PartsLibrary& lib, const std::vector<core::Brick>& leaves,
                                              const std::vector<const core::Brick*>& members) {
    std::vector<int> pick(leaves.size(), -1);
    for (size_t l = 0; l < leaves.size(); ++l) {
        for (size_t m = 0; m < members.size(); ++m) {
            if (members[m]->partNumber.compare(leaves[l].partNumber, Qt::CaseInsensitive) != 0) continue;
            if (pick[l] >= 0) return std::nullopt;  // the same part twice: not by joints
            pick[l] = static_cast<int>(m);
        }
        if (pick[l] < 0) return std::nullopt;
    }
    int joints = 0;
    bool hinged = false;
    for (size_t a = 0; a < leaves.size(); ++a) {
        const auto ma = lib.metadata(leaves[a].partNumber);
        if (!ma) return std::nullopt;
        for (size_t b = a + 1; b < leaves.size(); ++b) {
            const auto mb = lib.metadata(leaves[b].partNumber);
            if (!mb) return std::nullopt;
            for (int i = 0; i < ma->connections.size(); ++i) {
                for (int j = 0; j < mb->connections.size(); ++j) {
                    if (ma->connections[i].type.isEmpty() || ma->connections[i].type != mb->connections[j].type) continue;
                    const QPointF pa = parts::placement::connectionWorld(leaves[a], i, lib);
                    const QPointF pb = parts::placement::connectionWorld(leaves[b], j, lib);
                    if (std::hypot(pa.x() - pb.x(), pa.y() - pb.y()) > 0.05) continue;
                    // A joint of the set: the members must be linked there.
                    const core::Brick& A = *members[pick[a]];
                    const core::Brick& B = *members[pick[b]];
                    if (i >= static_cast<int>(A.connections.size()) || j >= static_cast<int>(B.connections.size())) return std::nullopt;
                    if (A.connections[i].linkedToId.isEmpty() || A.connections[i].linkedToId != B.connections[j].guid)
                        return std::nullopt;
                    const double hinge = connectionHingeAngle(ma->connections[i].type);
                    hinged |= hinge > 0.0;
                    const double setTurn = leaves[b].orientation - leaves[a].orientation;
                    const double memberTurn = B.orientation - A.orientation;
                    if (angleGap(memberTurn, setTurn) > hinge + 0.1) return std::nullopt;
                    ++joints;
                }
            }
        }
    }
    if (!hinged || joints < static_cast<int>(leaves.size()) - 1) return std::nullopt;
    return pick;
}

}  // namespace

ExpandedSet expandSet(parts::PartsLibrary& lib, const QString& key, QPointF centreStuds, double angleDegrees) {
    ExpandedSet out;
    const auto meta = lib.metadata(key);
    if (!meta || meta->kind != parts::PartKind::Group || meta->subparts.isEmpty()) return out;
    const QString canonical = lib.canonicalKey(key);
    expandInto(lib, *meta, canonical.isEmpty() ? key : canonical, centreStuds, angleDegrees, QString(), out, 0);
    return out;
}

std::vector<SetEnd> setAnchorOrder(parts::PartsLibrary& lib, const QString& setKey,
                                   const std::vector<const core::Brick*>& parts) {
    // The parts in sub-part order: each takes the first free slot with its
    // part number; any left over (not the set's) come last.
    const ExpandedSet set = expandSet(lib, setKey, QPointF(0, 0));
    std::vector<const core::Brick*> ordered(set.bricks.size(), nullptr);
    std::vector<const core::Brick*> extra;
    for (const core::Brick* b : parts) {
        bool placed = false;
        for (size_t i = 0; i < set.bricks.size() && !placed; ++i) {
            if (ordered[i] || set.bricks[i].partNumber.compare(b->partNumber, Qt::CaseInsensitive) != 0)
                continue;
            ordered[i] = b;
            placed = true;
        }
        if (!placed) extra.push_back(b);
    }
    std::vector<SetEnd> all;
    for (const auto* list : { &ordered, &extra }) {
        for (const core::Brick* b : *list) {
            if (!b) continue;
            const auto meta = lib.metadata(b->partNumber);
            const int n = meta ? static_cast<int>(meta->connections.size()) : 0;
            for (int c = 0; c < n; ++c) all.push_back({ b, c });
        }
    }
    const auto meta = lib.metadata(setKey);
    const QHash<int, int> next = meta ? meta->groupNextPreferred : QHash<int, int>();
    std::vector<SetEnd> out;
    std::vector<bool> taken(all.size(), false);
    for (int i = 0; i >= 0 && i < static_cast<int>(all.size()) && !taken[i]; i = next.value(i, -1)) {
        taken[i] = true;
        out.push_back(all[i]);
    }
    for (size_t i = 0; i < all.size(); ++i)
        if (!taken[i]) out.push_back(all[i]);
    return out;
}

std::vector<SetModule> findSetModules(const core::Map& map, parts::PartsLibrary& lib) {
    std::vector<SetModule> out;
    if (map.sidecar.modules.empty()) return out;
    // Sets by every name they go by (key, descriptions), lower case.
    QHash<QString, QStringList> setsByName;
    for (const QString& key : lib.keys()) {
        const auto meta = lib.metadata(key);
        if (!meta || meta->kind != parts::PartKind::Group || meta->subparts.isEmpty()) continue;
        setsByName[key.toLower()] << key;
        for (const auto& d : meta->descriptions)
            if (!d.text.trimmed().isEmpty()) setsByName[d.text.trimmed().toLower()] << key;
    }
    for (const core::Module& mod : map.sidecar.modules) {
        if (!defaultLook(mod) || mod.memberIds.isEmpty()) continue;
        const QStringList candidates = setsByName.value(mod.name.trimmed().toLower());
        if (candidates.isEmpty()) continue;
        // Every member a loose brick on one brick layer.
        int layerIndex = -1;
        std::vector<const core::Brick*> members;
        bool ok = true;
        for (int li = 0; li < static_cast<int>(map.layers().size()) && ok; ++li) {
            const auto* L = brickLayerAt(map, li);
            if (!L) continue;
            for (const auto& b : L->bricks) {
                if (!mod.memberIds.contains(b.guid)) continue;
                if (layerIndex >= 0 && layerIndex != li) { ok = false; break; }
                if (!b.myGroupId.isEmpty()) { ok = false; break; }
                layerIndex = li;
                members.push_back(&b);
            }
        }
        if (!ok || static_cast<int>(members.size()) != mod.memberIds.size()) continue;
        for (const QString& key : candidates) {
            const ExpandedSet set = expandSet(lib, key, QPointF(0, 0));
            if (set.bricks.size() != members.size()) continue;
            QStringList want, have;
            for (const auto& b : set.bricks) want << b.partNumber.toUpper();
            for (const auto* b : members) have << b->partNumber.toUpper();
            want.sort();
            have.sort();
            if (want != have) continue;
            auto pick = matchByLayout(lib, set.bricks, members);
            if (!pick) pick = matchByJoints(lib, set.bricks, members);
            if (!pick) continue;
            SetModule found;
            found.moduleId = mod.id;
            found.setKey = key;
            found.layerIndex = layerIndex;
            found.groups = set.groups;
            for (size_t l = 0; l < set.bricks.size(); ++l)
                found.parentOf.insert(members[(*pick)[l]]->guid, set.bricks[l].myGroupId);
            out.push_back(std::move(found));
            break;
        }
    }
    return out;
}

std::vector<SetModule> findLooseSets(const core::Map& map, parts::PartsLibrary& lib, const QSet<QString>& skip) {
    std::vector<SetModule> out;
    // The sets used whole, with their parts (upper case, sorted).
    struct Kind {
        QString key;
        QStringList parts;
    };
    std::vector<Kind> kinds;
    QSet<QString> setParts;
    for (const QString& key : lib.keys()) {
        const auto meta = lib.metadata(key);
        if (!meta || meta->kind != parts::PartKind::Group || meta->canUngroup || meta->subparts.isEmpty()) continue;
        const ExpandedSet set = expandSet(lib, key, QPointF(0, 0));
        if (set.bricks.size() < 2) continue;
        Kind k{ key, {} };
        for (const auto& b : set.bricks) {
            k.parts << b.partNumber.toUpper();
            setParts.insert(b.partNumber.toUpper());
        }
        k.parts.sort();
        kinds.push_back(std::move(k));
    }
    if (kinds.empty()) return out;
    for (int li = 0; li < static_cast<int>(map.layers().size()); ++li) {
        const auto* L = brickLayerAt(map, li);
        if (!L) continue;
        const auto loose = [&](const core::Brick& b) {
            return b.myGroupId.isEmpty() && !skip.contains(b.guid) && setParts.contains(b.partNumber.toUpper());
        };
        // Loose set parts by their connections' ids.
        QHash<QString, int> byConnection;
        for (int i = 0; i < static_cast<int>(L->bricks.size()); ++i)
            if (loose(L->bricks[i]))
                for (const auto& c : L->bricks[i].connections)
                    if (!c.guid.isEmpty()) byConnection.insert(c.guid, i);
        std::vector<bool> seen(L->bricks.size(), false);
        for (int i = 0; i < static_cast<int>(L->bricks.size()); ++i) {
            if (seen[i] || !loose(L->bricks[i])) continue;
            // The parts joined to it at hinges, and to those, and so on.
            std::vector<int> joined{ i };
            seen[i] = true;
            for (size_t n = 0; n < joined.size() && joined.size() <= 16; ++n) {
                const core::Brick& b = L->bricks[joined[n]];
                const auto meta = lib.metadata(b.partNumber);
                if (!meta) continue;
                const int count = std::min(static_cast<int>(meta->connections.size()), static_cast<int>(b.connections.size()));
                for (int c = 0; c < count; ++c) {
                    if (connectionHingeAngle(meta->connections[c].type) <= 0.0f) continue;
                    const int j = byConnection.value(b.connections[c].linkedToId, -1);
                    if (j < 0 || seen[j]) continue;
                    seen[j] = true;
                    joined.push_back(j);
                }
            }
            if (joined.size() < 2) continue;
            std::vector<const core::Brick*> members;
            QStringList have;
            for (int j : joined) {
                members.push_back(&L->bricks[j]);
                have << L->bricks[j].partNumber.toUpper();
            }
            have.sort();
            for (const Kind& k : kinds) {
                if (k.parts != have) continue;
                const ExpandedSet set = expandSet(lib, k.key, QPointF(0, 0));
                const auto pick = matchByJoints(lib, set.bricks, members);
                if (!pick) continue;
                SetModule found;
                found.setKey = k.key;
                found.layerIndex = li;
                found.groups = set.groups;
                for (size_t l = 0; l < set.bricks.size(); ++l)
                    found.parentOf.insert(members[(*pick)[l]]->guid, set.bricks[l].myGroupId);
                out.push_back(std::move(found));
                break;
            }
        }
    }
    return out;
}

QUndoCommand* makeSetsCommand(core::Map& map, const std::vector<SetModule>& sets) {
    auto* parent = new QUndoCommand(QObject::tr("Make %n set(s) of their modules", nullptr, static_cast<int>(sets.size())));
    QHash<int, std::pair<Grouping, Grouping>> byLayer;
    for (const SetModule& s : sets) {
        if (!s.moduleId.isEmpty()) new DeleteModuleCommand(map, s.moduleId, parent);
        auto it = byLayer.find(s.layerIndex);
        if (it == byLayer.end()) {
            const Grouping g = captureGrouping(map, s.layerIndex);
            it = byLayer.insert(s.layerIndex, { g, g });
        }
        Grouping& after = it->second;
        for (const auto& g : s.groups) after.groups.push_back(g);
        for (auto p = s.parentOf.constBegin(); p != s.parentOf.constEnd(); ++p) after.parentOf[p.key()] = p.value();
    }
    std::vector<Grouping> before, after;
    for (auto it = byLayer.cbegin(); it != byLayer.cend(); ++it) {
        before.push_back(it->first);
        after.push_back(it->second);
    }
    new SetGroupingCommand(map, std::move(before), std::move(after), parent);
    return parent;
}

}  // namespace bld::edit
