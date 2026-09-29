#pragma once

// core::Map → the web editor's layout model, the inverse of WebModel.h:
// the JSON shapes the shared document stores (meta, layer order, layer
// data with bricks, groups, text cells, areas and rulers).

#include <QJsonObject>

namespace bld::core {
class Map;
struct Sidecar;
} // namespace bld::core

namespace bld::sync {

// { meta, layers, layerData } as the web's bbmToDoc lays them out.
QJsonObject docJsonFromMap(const core::Map& map);

// The shared document's sidecar cache (meta.cache: anchored labels,
// modules, venue) after the desktop's sidecar is written into `current`.
// Last write wins per part; parts the desktop doesn't handle (the web's
// background image) are kept, and an empty list stays absent.
QJsonObject mergeSidecarCache(const QJsonObject& current, const core::Sidecar& sidecar);

}  // namespace bld::sync
