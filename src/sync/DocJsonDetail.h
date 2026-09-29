#pragma once

// Shared by DocJson.cpp and SyncDoc.cpp: a yrs doc's layout root types as
// JSON (see DocJson.h).

#include <QJsonObject>

struct YDoc;

namespace bld::sync::detail {

QJsonObject rootsToJson(YDoc* doc);

}  // namespace bld::sync::detail
