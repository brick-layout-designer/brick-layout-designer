#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::import {

struct MapReadResult {
    std::unique_ptr<core::Map> map;
    QString     error;
    QStringList warnings;
    bool ok() const { return map != nullptr; }
};

// BlueBrick's LDraw map format, ported from its SaveLoadManager so files
// travel between the two tools:
//   .ldr  every "0 STEP" starts a new layer
//   .mpd  every submodel ("0 FILE") is a layer named after it; the main
//         model's MLCAD HIDE marks hidden layers
// Bricks are placed with each part's <LDraw> remap; parts BlueBrickParts
// marks as ignorable (sleeper plates) are skipped. MLCAD groups (BTG /
// GROUP), "0 !BLUEBRICK RULER" lines and the Author / LUG / Event / Date /
// comment header are understood.
MapReadResult readLDrawMap(const QString& path, parts::PartsLibrary& lib);

// Write `map` as .ldr or .mpd (chosen by `path`'s extension) the way
// BlueBrick does: remapped parts, sleepers under rails, groups, rulers.
// Area, grid and text layers can't be expressed in LDraw and are written
// as a comment only. Parts without a numeric color (sets, logos) are
// skipped, as in BlueBrick.
bool writeLDrawMap(const core::Map& map, const QString& path,
                   parts::PartsLibrary& lib, QString* error = nullptr);

}  // namespace bld::import
