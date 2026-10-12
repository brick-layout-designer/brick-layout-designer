#pragma once

// The web keeps the shared layout's modules and anchored labels twice: in
// meta.cache (the whole sidecar, one value) and one per key in the
// `sidecarItems` root map ("m:<id>" / "l:<id>", each with its `_order`), so
// two people changing different ones at once both keep their change.
// `sidecarItems.__blob` records which meta.cache the items go with: a hash
// of the blob's key-sorted JSON (web packages/ydoc sidecarBlobHash). Reading
// uses the items only when that hash matches; writing writes both.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace bld::sync {

// JSON with every object's keys sorted, numbers and strings written as
// JavaScript's JSON.stringify writes them.
QString stableJson(const QJsonValue& value);

// "<UTF-16 length>:<FNV-1a hex>" of stableJson(blob), as the web computes it.
QString sidecarBlobHash(const QJsonValue& blob);

// The sidecar as everyone's edits left it: `blob` with its modules and
// anchored labels taken from `items` when they go with this blob.
// False when a module or label has no id or shares one (read the blob as is).
bool sidecarIdsUnique(const QJsonObject& blob);
QJsonObject mergedSidecarCache(const QJsonObject& blob, const QJsonObject& items);

// What `sidecarItems` should hold for `blob`: one entry per module and
// anchored label, and `__blob`.
QJsonObject sidecarItemsFor(const QJsonObject& blob);

}  // namespace bld::sync
