#pragma once

// core::Map → the web editor's layout model, the inverse of WebModel.h:
// the JSON shapes the shared document stores (meta, layer order, layer
// data with bricks, groups, text cells, areas and rulers).

#include <QJsonObject>

namespace bld::core { class Map; }

namespace bld::sync {

// { meta, layers, layerData } as the web's bbmToDoc lays them out.
QJsonObject docJsonFromMap(const core::Map& map);

}  // namespace bld::sync
