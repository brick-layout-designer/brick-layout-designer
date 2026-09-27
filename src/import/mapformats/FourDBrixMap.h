#pragma once

#include "LDrawMap.h"  // MapReadResult

namespace bld::import {

// 4DBrix nControl (.ncp) maps, ported from BlueBrick's SaveLoadManager.
// Parts map through their <FourDBrix> remap. Track segments are
// placed by their origin node; tables, baseplates and structures by their
// image corner or centre. On load they go to "Tables", "Baseplates",
// "Tracks" and "Structures" layers, and segment groups are kept.
// 4DBrix names with no remap are listed in the warnings.
MapReadResult readFourDBrixMap(const QString& path, parts::PartsLibrary& lib);
bool writeFourDBrixMap(const core::Map& map, const QString& path,
                       parts::PartsLibrary& lib, QString* error = nullptr);

}  // namespace bld::import
