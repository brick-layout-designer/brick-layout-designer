#pragma once

// The whole shared layout document as JSON — what the web's
// `doc.toJSON()` returns: meta, layers (the layer id order) and layerData
// (id → layer), plus the sidecar maps (labels, modules, venue). Nested
// Y.Maps / Y.Arrays become objects / arrays; values stored as plain JSON
// stay as they are. WebModel.h maps it onto core::Map.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace bld::sync {

// Apply a Yjs v1 state update to a fresh doc and return its root types as
// JSON. nullopt (and *error set) when the update can't be applied.
std::optional<QJsonObject> docToJson(const QByteArray& update, QString* error = nullptr);

}  // namespace bld::sync
