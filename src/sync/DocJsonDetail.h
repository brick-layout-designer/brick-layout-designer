#pragma once

// Shared by DocJson.cpp and SyncDoc.cpp: a yrs doc's layout root types as
// JSON (see DocJson.h).

#include <QJsonObject>
#include <QJsonValue>

#include <array>

struct YDoc;
struct YOutput;
typedef struct TransactionInner YTransaction;

namespace bld::sync::detail {

// The root types the layout JSON has, in the order rootsToJson reads them:
// Y.Maps, then the one Y.Array ("layers").
inline constexpr std::array<const char*, 7> kRootNames = { "meta",  "layerData",    "labels", "modules",
                                                           "venue", "sidecarItems", "layers" };
inline constexpr std::size_t kLayerDataRoot = 1;
inline constexpr std::size_t kLayersRoot = 6;

QJsonObject rootsToJson(YDoc* doc);

// One value as JSON, `depth` levels below its root type (a root's own
// entries are 1 deep): past 64 levels a value reads as null, as in
// rootsToJson.
QJsonValue valueToJson(const YOutput* o, const YTransaction* txn, int depth);

}  // namespace bld::sync::detail
