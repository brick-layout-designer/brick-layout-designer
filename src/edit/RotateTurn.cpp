#include "RotateTurn.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QHash>
#include <QSet>

#include <cmath>

namespace bld::edit {

namespace {
struct Conn {
    const core::Brick* brick = nullptr;
    int index = -1;
};
double clamp360(double a) {
    if (a >= 360.0) a -= 360.0;
    if (a < 0.0) a += 360.0;
    return a;
}
}  // namespace

std::optional<ConnectedTurn> connectedTurn(const core::Map& map, const std::vector<const core::Brick*>& selected, int steps,
                                           parts::PartsLibrary& lib) {
    if (selected.empty() || steps == 0) return std::nullopt;
    // Every connection on the map by its id.
    QHash<QString, Conn> byId;
    for (const auto& l : map.layers()) {
        if (l->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
            for (int i = 0; i < static_cast<int>(b.connections.size()); ++i)
                if (!b.connections[i].guid.isEmpty()) byId.insert(b.connections[i].guid, { &b, i });
    }
    QSet<const core::Brick*> picked(selected.begin(), selected.end());
    // Free or joined outside, by connection type, in order (FreeConnectionSet).
    QHash<QString, std::vector<Conn>> available;
    QHash<QString, std::vector<Conn>> external;
    QStringList typeOrder;
    for (const core::Brick* b : selected) {
        const auto meta = lib.metadata(b->partNumber);
        if (!meta) continue;
        for (int i = 0; i < static_cast<int>(b->connections.size()) && i < meta->connections.size(); ++i) {
            const QString& type = meta->connections[i].type;
            if (!typeOrder.contains(type)) typeOrder.append(type);
            const QString& link = b->connections[i].linkedToId;
            const auto other = link.isEmpty() ? byId.end() : byId.find(link);
            if (link.isEmpty() || other == byId.end()) {
                available[type].push_back({ b, i });
            } else if (!picked.contains(other->brick)) {
                available[type].push_back({ b, i });
                external[type].push_back({ b, i });
            }
        }
    }
    // The type with the most joins outside (the first on a tie).
    QString chosen;
    std::size_t best = 0;
    for (const QString& t : typeOrder)
        if (external.value(t).size() > best) {
            best = external.value(t).size();
            chosen = t;
        }
    if (best == 0) return std::nullopt;
    const Conn old = external[chosen].front();
    const Conn fixed = byId.value(old.brick->connections[old.index].linkedToId);
    const auto& list = available[chosen];
    const int n = static_cast<int>(list.size());
    if (n == 0) return std::nullopt;
    int index = 0;
    while (index < n && (list[index].brick != old.brick || list[index].index != old.index)) ++index;
    index = ((index + steps) % n + n) % n;
    const Conn next = list[index];
    const auto fixedMeta = lib.metadata(fixed.brick->partNumber);
    const auto nextMeta = lib.metadata(next.brick->partNumber);
    if (!fixedMeta || !nextMeta || fixed.index >= fixedMeta->connections.size()) return std::nullopt;
    // AddConnectBrick.sGetOrientationOfConnectedBrick.
    const double diff = clamp360(fixedMeta->connections[fixed.index].angleDegrees + 180.0
                                 - nextMeta->connections[next.index].angleDegrees);
    const double orientation = clamp360(fixed.brick->orientation + diff);
    ConnectedTurn out;
    out.degrees = orientation - next.brick->orientation;
    out.brickGuid = next.brick->guid;
    out.connection = next.index;
    out.at = parts::placement::connectionWorld(*old.brick, old.index, lib);
    return out;
}

}  // namespace bld::edit
