#pragma once

// Sets from the parts library (a <group> XML, such as flex.group): placed as
// a real BlueBrick group, and found again among modules that older builds
// made when a set was placed.

#include "../core/Brick.h"
#include "../core/Group.h"

#include <QHash>
#include <QPointF>
#include <QString>
#include <QUndoCommand>

#include <vector>

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

// A set's parts and groups, as BlueBrick's Group(string) constructor builds
// them: every sub-part at its world transform, nested sets as child groups.
// The outermost group is groups.front().
struct ExpandedSet {
    std::vector<core::Brick> bricks;
    std::vector<core::Group> groups;
};

// `key` placed with its set-local origin at `centreStuds`, turned
// `angleDegrees`. Empty when `key` isn't a set.
ExpandedSet expandSet(parts::PartsLibrary& lib, const QString& key, QPointF centreStuds, double angleDegrees = 0.0);

// A connection of a placed set's part.
struct SetEnd {
    const core::Brick* brick = nullptr;
    int connection = -1;
};

// Where to add the next part beside a placed set `setKey` made of `parts`:
// every connection of its parts, in the order to try them. The set's
// connections are numbered in sub-part order, as BlueBrick counts them,
// and its <GroupConnectionPreferenceList> is followed from connection 0
// (flex.group: 0, then 2, its two rail ends); the rest follow in order.
std::vector<SetEnd> setAnchorOrder(parts::PartsLibrary& lib, const QString& setKey,
                                   const std::vector<const core::Brick*>& parts);

// A module that is exactly one placed set.
struct SetModule {
    QString moduleId;
    QString setKey;
    int layerIndex = -1;
    std::vector<core::Group> groups;        // to add, fresh guids
    QHash<QString, QString> parentOf;       // member brick guid -> its group
};

// Modules that are exactly a set placement: one brick layer; the set's parts
// (counted through nested sets) and nothing else, none already grouped; at
// the set's relative positions and angles, within 0.02 stud and 0.1 degree,
// or for a set of distinct parts joined by hinges (flex track), linked at
// each joint and bent within its hinge angle; still named after the set
// (its key or a description); unpinned, with the default look.
std::vector<SetModule> findSetModules(const core::Map& map, parts::PartsLibrary& lib);

// Turns those modules into sets (removes the modules, adds the groups) in
// one undo step.
QUndoCommand* makeSetsCommand(core::Map& map, const std::vector<SetModule>& sets);

}  // namespace bld::edit
