#pragma once

// Three-way compare and merge for reconnecting after offline edits (sync
// phase P4b, DESKTOP-LIVE-SYNC.md "Offline edits and reconnecting"): the
// copy the desktop went offline with (base), its offline edits (mine) and
// the server's layout now. Items and their keys are the server's compare
// (web apps/server/src/sync/compare.ts), so both report the same changes:
//
//   map                        header: author, LUG, event, date, comment, background
//   layer:<id>                 a layer's own properties (not its items)
//   brick:<layer>:<id>         bricks, compared without their connection links
//   group:<layer>:<id>
//   text:<layer>:<id|#index>   text cells
//   area:<layer>:<x>,<y>       area cells
//   ruler:<layer>:<hash>       rulers, keyed by content
//   label:<id> module:<id> venue background   sidecar data

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <memory>

namespace bld::core { class Map; }

namespace bld::sync::merge {

// A layout as the shared document holds it: the doc JSON (docJsonFromMap)
// and the sidecar JSON (sidecarToJson).
struct Snapshot {
    QJsonObject doc;
    QJsonObject sidecar;
};
Snapshot snapshotOf(const core::Map& map);

enum class Side { Unchanged, Added, Edited, Deleted };
// Mine / Server: only that side changed. Same: both made the same change.
enum class Status { Mine, Server, Same, Conflict };
// What to keep of a change: my side, the server's, or both (an item both
// sides have, mine added again as a copy).
enum class Choice { Mine, Server, Both };

struct ItemChange {
    QString key;
    QString kind;     // map, layer, brick, group, text, area, ruler, label, module, venue, background
    QString layerId;  // for layer items
    Status status = Status::Mine;
    Side mine = Side::Unchanged;
    Side server = Side::Unchanged;
    // Undefined where that side has no such item.
    QJsonValue base{ QJsonValue::Undefined }, mineValue{ QJsonValue::Undefined }, serverValue{ QJsonValue::Undefined };

    // Choices this change allows: Both only where both sides have the item
    // and it can be copied (bricks, text cells, labels, modules).
    bool allowsBoth() const;
    // What a merge does without being told: my change unless it clashes.
    Choice defaultChoice() const { return status == Status::Mine ? Choice::Mine : Choice::Server; }
};

// Every item that differs from the base on either side, sorted by key.
QList<ItemChange> compareLayouts(const Snapshot& base, const Snapshot& mine, const Snapshot& server);

// The server's layout with my side of the changes put in: each change's
// choice from `choices` (by key), or its default. The result is a desktop
// map, to be written to the shared document as an ordinary edit; nullptr
// (and *error) if it can't be read back.
std::unique_ptr<core::Map> mergeLayouts(const Snapshot& mine, const Snapshot& server,
                                        const QList<ItemChange>& changes, const QHash<QString, Choice>& choices,
                                        QString* error = nullptr);

// One line saying what a change is, for the compare window.
QString describe(const ItemChange& change);

}  // namespace bld::sync::merge
