#pragma once

#include "LDrawMap.h"  // MapReadResult

namespace bld::import {

// TrackDesigner (.tdl, file version 20) maps, ported from BlueBrick's
// SaveLoadManager. Each TD piece maps to a BlueBrickParts part through the
// part's <TrackDesigner> remap (TD id, per-port connection and angle). On
// load, bricks go to "Baseplate", "Rail" and "Monorail" layers by their
// connection type; TD ids without a remap are listed in the warnings.
//
// BlueBrick picks TD part ids for the TrackDesigner "registry" configured
// in Windows; here it is always the default registry. Connection polarity
// is written as "unassigned".
MapReadResult readTrackDesignerMap(const QString& path, parts::PartsLibrary& lib);
bool writeTrackDesignerMap(const core::Map& map, const QString& path,
                           parts::PartsLibrary& lib, QString* error = nullptr);

}  // namespace bld::import
