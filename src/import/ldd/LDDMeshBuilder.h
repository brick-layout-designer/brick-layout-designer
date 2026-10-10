#pragma once

#include "../../geom/Mesh.h"
#include "../ldraw/LDrawReader.h"
#include "LDDLDrawMapping.h"
#include "LDDMaterials.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace bld::import {

class LDDAssets;

// Bake an LDD model's brick refs into a flat geom::Mesh by loading
// each part's `.g` geometry directly (bypassing the LDraw library
// and the ldraw.xml mapping).
//
// Used as the fallback path for LDD parts that have no ldraw.xml
// mapping (LDD-only decorations, exclusive prints) — the LDraw
// pipeline can't render those because there's no .dat. With this
// path we render LDD's own geometry and paint it with the LDD
// material color from Materials.xml.
//
// .g bytes come from an LDDAssets (db.lif, nested or extracted).
//
// LDDLDrawBakedModel reports which design IDs we successfully
// rendered and which were skipped, so the caller's UI can summarise.
struct LDDLDrawBakedModel {
    geom::Mesh    mesh;
    QStringList   errors;
    int           rendered = 0;        // refs that produced geometry
    int           skipped  = 0;        // refs whose .g couldn't be found
};

class LDDMeshBuilder {
public:
    // Where brick geometry comes from ("Primitives/LOD0/<id>.g" in LDD's
    // database). Must outlive this builder.
    void setAssets(const LDDAssets* assets) { assets_ = assets; }

    void setMaterials(const LDDMaterials* materials) { materials_ = materials; }
    void setMapping(const LDDLDrawMapping* mapping)  { mapping_   = mapping; }

    // Bake every ref in `read` whose designID is NOT covered by the
    // ldraw.xml mapping. Refs that ARE in the mapping are skipped —
    // they go through the LDraw pipeline instead. The result is a
    // partial mesh containing only LDD-only-rendered parts; callers
    // typically merge it with the LDraw-baked mesh before
    // rasterizing.
    LDDLDrawBakedModel bake(const LDrawReadResult& read);

private:
    QByteArray fetchPart(const QString& designId);

    const LDDAssets*       assets_    = nullptr;
    const LDDMaterials*    materials_ = nullptr;
    const LDDLDrawMapping* mapping_   = nullptr;
};

}  // namespace bld::import
