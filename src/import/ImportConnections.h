#pragma once

#include "ImportToPart.h"

#include <QPointF>
#include <QVector>

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::import {

// The connection points an imported model exposes to the outside world,
// for the <ConnexionList> of the library part it becomes.
//
// `map` is the model as placed by toBlueBrickMap(read, &lib, ...). Its
// connectivity is rebuilt first, so connection points of two bricks that
// meet inside the model link up and only the free ends are returned.
// Positions are relative to `partOriginStuds` (the new sprite's centre,
// in the map's stud coordinates), angles in the map's frame. Bricks whose
// part isn't in `lib` contribute nothing.
QVector<ImportedConnection> externalConnections(core::Map& map,
                                                parts::PartsLibrary& lib,
                                                QPointF partOriginStuds);

}  // namespace bld::import
