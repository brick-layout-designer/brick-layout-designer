#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "../ldraw/LDrawReader.h"

namespace bld::import {

// Parses LEGO Digital Designer's `ldraw.xml` — the rosetta-stone file
// that maps LDD design IDs and material IDs to LDraw equivalents. With
// this loaded we can render an LDD model through the LDraw pipeline:
//
//   <Brick ldraw="3001.dat" lego="3001" />
//     LDD designID 3001 → LDraw 3001.dat
//
//   <Material ldraw="14" lego="24" />
//     LDD materialID 24 → LDraw colour code 14
//
//   <Transformation ldraw="3001.dat" tx="..." ty="..." tz="..."
//                   ax="..." ay="..." az="..." angle="..." />
//     LDD vs. LDraw coord-frame correction for that part. Applied
//     after the LDD <Brick transformation> when rendering.
//
// Not every LDD design ID has an LDraw equivalent — Digital Designer
// has parts (decorations especially) that aren't in the LDraw library.
// `partFor()` returns an empty string for those; the caller can then
// fall back to LDD's own geometry from db.lif.
class LDDLDrawMapping {
public:
    LDDLDrawMapping() = default;

    // Parse from disk. Returns false on file-open failure or empty
    // result. Successive loads merge into the existing maps.
    bool loadFromFile(const QString& path);

    // Look up an LDD design ID. Returns the LDraw .dat filename (e.g.
    // "3001.dat") or an empty string when no mapping exists.
    QString partFor(const QString& lddDesignId) const {
        return brickToLdraw_.value(lddDesignId);
    }

    // Every LDraw file ldraw.xml maps this design ID to, oldest first
    // (partFor() is the last one, which lxf2ldr uses too).
    QStringList partsFor(const QString& lddDesignId) const {
        return brickAliases_.value(lddDesignId);
    }

    // Look up an LDD material ID. Returns -1 when no mapping exists.
    int colourFor(int lddMaterialId) const {
        return materialToLdraw_.value(lddMaterialId, -1);
    }

    // Per-part transformation correction (axis-angle + translation).
    // Empty fields in the source XML mean "no correction"; we return
    // an identity-equivalent struct in that case.
    struct Transformation {
        double tx = 0.0, ty = 0.0, tz = 0.0;
        double ax = 0.0, ay = 1.0, az = 0.0;
        double angle = 0.0;
        bool   exists = false;
    };
    Transformation transformFor(const QString& ldrawDat) const {
        return transformations_.value(ldrawDat);
    }

    // Convert a readLDD() result (LDD axes, LDD design/material IDs) into
    // the equivalent LDraw model: LDraw part files and colours, LDraw
    // axes, and each part's <Transformation> correction applied, exactly
    // as the community lxf2ldr converter does. Design IDs without a
    // mapping keep "<designID>.dat"; unmapped materials keep their ID.
    LDrawReadResult toLDraw(const LDrawReadResult& ldd) const;

    int brickCount() const  { return brickToLdraw_.size(); }
    int materialCount() const { return materialToLdraw_.size(); }
    int transformCount() const { return transformations_.size(); }

private:
    QHash<QString, QString>    brickToLdraw_;        // LDD designID → "X.dat"
    QHash<QString, QStringList> brickAliases_;       // LDD designID → every "X.dat"
    QHash<int, int>            materialToLdraw_;     // LDD materialID → LDraw code
    QHash<QString, Transformation> transformations_;  // "X.dat" → correction
};

}  // namespace bld::import
