#pragma once

#include "../../geom/Mesh.h"
#include "LDrawLibrary.h"
#include "LDrawMeshLoader.h"
#include "LDrawPalette.h"
#include "LDrawReader.h"

#include <QString>
#include <QStringList>

namespace bld::import {

// Bake an entire parsed LDraw / Studio model into one flat triangle
// mesh, in model coords (LDU), converted to geom::Mesh's +Y-up frame
// (LDraw y and z negated). Walks every type-1 ref in the
// model, resolves each against the library, applies the ref's color +
// transform, and concatenates the resolved part meshes into a single
// model-level mesh ready for the rasterizer.
//
// This is the path that does NOT depend on BlueBrickParts. Pieces
// resolve against the user-pointed LDraw library and render via the
// mesh-loader / rasterizer pipeline. Any part not in the library is
// reported in `errors` and skipped (model still renders the parts
// that did resolve).
//
// Inline primitives carried in the source file (type-3 / type-4 lines)
// are also baked in — Studio sometimes inlines geometry that has no
// equivalent .dat file, so we fold those into the model mesh as if
// they were a single zero-transform "ref".
struct BakedModel {
    geom::Mesh    mesh;
    QStringList   errors;
    int           resolvedRefs   = 0;
    int           unresolvedRefs = 0;
};

BakedModel bakeMeshFromLDraw(const LDrawReadResult& src,
                             LDrawMeshLoader& loader,
                             const LDrawPalette& palette);

}  // namespace bld::import
