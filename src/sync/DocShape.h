#pragma once

// Doc shape 2 (meta.schemaVersion 2; the web's packages/ydoc shape.ts):
// edits that merge with someone else's at the same moment, because they
// change entries in place instead of deleting and inserting copies.
//
//  - Ruler items are Y.Maps (as parts and text cells are).
//  - An area sheet has a `cellOffset` {x, y} in whole cells: a cell stored
//    at (x, y) is drawn at (x + offset.x, y + offset.y). Moving the sheet
//    sets the offset.
//  - Parts and rulers may have a number `z`: the drawing order is by z,
//    not by place in the array, so a reorder sets z on the same entry.
//
// Docs in shape 1 read the same; a doc moves to 2 the first time an edit
// needs it (in an untracked step), and builds that read only 1 then refuse
// it. Shared vectors: fixtures/contracts/doc-shape.json.

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

namespace bld::sync {

// The shape a doc is made in, and the one with in-place edits.
constexpr int kDocBaseSchema = 1;
constexpr int kDocShapeInPlace = 2;

// The z of a doc entry (a JSON object), if it has one.
std::optional<double> zOf(const QJsonValue& item);

// The drawing order (indexes, bottom first) of entries with these z: an
// entry without z sorts right after the last entry before it that has one
// (first when none does); ties keep the array order.
QList<qsizetype> orderByZ(const QList<std::optional<double>>& zs);

// The array's entries in drawing order.
QJsonArray inZOrder(const QJsonArray& items);

// Whether any entry of the array has a z.
bool hasZOrder(const QJsonArray& items);

struct ZEntry {
    QString id;
    std::optional<double> z;
};

// The z to set so the order becomes `want` (ids, bottom first), given the
// entries in their order now: the longest run of entries with a z already
// in order keeps it, every other entry (new ones too) gets one between its
// neighbours. Only the ones that change, in `want` order.
QList<std::pair<QString, double>> assignZ(const QList<ZEntry>& cur, const QStringList& want);

} // namespace bld::sync
