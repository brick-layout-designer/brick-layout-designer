#pragma once

// Groups in a brick layer, as BlueBrick nests them: an item (brick or
// group) names its parent group with <MyGroup>; a group from the library
// (a "set", such as flex.group) has a part number, a user's group none.

#include "LayerBrick.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

namespace bld::core {

inline const Group* findGroup(const LayerBrick& layer, const QString& guid) {
    if (guid.isEmpty()) return nullptr;
    for (const auto& g : layer.groups)
        if (g.guid == guid) return &g;
    return nullptr;
}

inline Group* findGroup(LayerBrick& layer, const QString& guid) {
    return const_cast<Group*>(findGroup(static_cast<const LayerBrick&>(layer), guid));
}

// `groupId` and its parents, innermost first (cycles and dangling ids stop it).
inline QStringList groupChain(const LayerBrick& layer, QString groupId) {
    QStringList chain;
    while (!groupId.isEmpty() && !chain.contains(groupId) && chain.size() < 64) {
        const Group* g = findGroup(layer, groupId);
        if (!g) break;
        chain << groupId;
        groupId = g->myGroupId;
    }
    return chain;
}

// The outermost group above an item whose parent is `groupId`, or empty.
inline QString topGroup(const LayerBrick& layer, const QString& groupId) {
    const QStringList chain = groupChain(layer, groupId);
    return chain.isEmpty() ? QString() : chain.back();
}

// Guids of the bricks under a group, at any depth.
inline QSet<QString> bricksUnder(const LayerBrick& layer, const QString& groupGuid) {
    QSet<QString> out;
    if (groupGuid.isEmpty()) return out;
    for (const auto& b : layer.bricks)
        if (groupChain(layer, b.myGroupId).contains(groupGuid)) out.insert(b.guid);
    return out;
}

// BlueBrick's LayerItem.TopNamedItem: climbing from a brick through parents
// that have a part number. Returns that group's guid, or empty when the
// brick itself is the item (no named parent).
inline QString topNamedGroup(const LayerBrick& layer, const Brick& brick) {
    QString item;
    for (const QString& id : groupChain(layer, brick.myGroupId)) {
        const Group* g = findGroup(layer, id);
        if (!g || g->partNumber.isEmpty()) break;
        item = id;
    }
    return item;
}

// BlueBrick's LibraryBrickList: the part numbers a part list counts, a set
// once (its top named group) and a loose brick by itself.
inline QStringList libraryItems(const LayerBrick& layer) {
    QStringList out;
    QSet<QString> counted;
    for (const auto& b : layer.bricks) {
        const QString g = topNamedGroup(layer, b);
        if (g.isEmpty()) {
            out << b.partNumber;
        } else if (!counted.contains(g)) {
            counted.insert(g);
            out << findGroup(layer, g)->partNumber;
        }
    }
    return out;
}

// Copies of `source`'s groups above `bricks` (copies of its bricks, still
// naming the old groups), under fresh guids; the bricks are moved to the
// new ids. Groups not above any of the bricks are left out.
template <typename NewId>
std::vector<Group> cloneGroups(const LayerBrick& source, std::vector<Brick>& bricks, NewId newId) {
    QHash<QString, QString> renamed;
    std::vector<Group> out;
    for (auto& b : bricks) {
        const QStringList chain = groupChain(source, b.myGroupId);
        for (const QString& id : chain) {
            if (renamed.contains(id)) continue;
            Group copy = *findGroup(source, id);
            renamed.insert(id, newId());
            copy.guid = renamed.value(id);
            out.push_back(copy);
        }
        b.myGroupId = chain.isEmpty() ? QString() : renamed.value(chain.front());
    }
    for (auto& g : out) g.myGroupId = renamed.value(g.myGroupId);  // a parent outside the copy: none
    return out;
}

// Groups with no brick under them (left behind by a delete).
inline QSet<QString> emptyGroups(const LayerBrick& layer) {
    QSet<QString> used;
    for (const auto& b : layer.bricks)
        for (const QString& id : groupChain(layer, b.myGroupId)) used.insert(id);
    QSet<QString> out;
    for (const auto& g : layer.groups)
        if (!used.contains(g.guid)) out.insert(g.guid);
    return out;
}

}  // namespace bld::core
