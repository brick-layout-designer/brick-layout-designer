#pragma once

// The web editor's layout model (as the shared document stores it, see
// DocJson.h) mapped onto core::Map. Sync phase P3a: the desktop reads a
// server layout. Field names follow the web's @cld/model BbmMap; value
// conventions follow the desktop's .bbm reader, so a layout that came from
// a .bbm writes back to the same bytes.

#include <QJsonObject>
#include <QString>

#include <memory>

namespace bld::core { class Map; }

namespace bld::sync {

// nullptr (and *error set) when the document lacks its header or a layer
// is malformed. Sidecar data (labels, modules, venue) isn't mapped yet.
std::unique_ptr<core::Map> mapFromDocJson(const QJsonObject& doc, QString* error = nullptr);

}  // namespace bld::sync
