#include "LDrawMeshBuilder.h"

namespace bld::import {

namespace {

// Convert a 3x3 rotation + (x,y,z) translation from an LDraw type-1
// line into a geom::Mat4. The 3x3 matrix is in row-major.
geom::Mat4 refTransform(const LDrawPartRef& ref) {
    return geom::Mat4::fromTranslationAndRotation(
        geom::Vec3{ ref.x, ref.y, ref.z },
        ref.m[0], ref.m[1], ref.m[2],
        ref.m[3], ref.m[4], ref.m[5],
        ref.m[6], ref.m[7], ref.m[8]);
}

// LDraw is -Y up; geom::Mesh (and the rasterizer) is +Y up. A 180°
// rotation about X (negate Y and Z) converts between them without
// mirroring. Skipping it rendered every LDraw import from underneath:
// mirror-imaged, bottoms visible, prints hidden. The resulting Z also
// matches BlueBrick's LDraw convention (BlueBrick y = -LDraw z).
geom::Vec3 toMeshFrame(geom::Vec3 v) { return { v.x, -v.y, -v.z }; }

void convertToMeshFrame(geom::Mesh& mesh) {
    for (auto& t : mesh.tris) {
        for (int k = 0; k < 3; ++k) {
            t.v[k] = toMeshFrame(t.v[k]);
            t.n[k] = toMeshFrame(t.n[k]);
        }
    }
    for (auto& e : mesh.edges) {
        e.v[0] = toMeshFrame(e.v[0]);
        e.v[1] = toMeshFrame(e.v[1]);
    }
}

}  // namespace

BakedModel bakeMeshFromLDraw(const LDrawReadResult& src,
                              LDrawMeshLoader& loader,
                              const LDrawPalette& palette) {
    BakedModel out;

    // First pass: resolved subfile refs. Each ref's mesh comes back in
    // part-local coords; we transform it into model coords by walking
    // every triangle through the ref's 4x4. The mesh-loader caches the
    // per-part bake, so a model that uses the same brick a hundred
    // times only parses the .dat once. Every part is looked up first so
    // the output is allocated once at its final size: growing it part by
    // part copied the whole mesh thousands of times on a big set.
    std::vector<std::shared_ptr<const geom::Mesh>> meshes;
    meshes.reserve(src.parts.size());
    std::size_t tris = 0, edges = 0;
    for (const auto& ref : src.parts) {
        auto mesh = loader.loadPartShared(ref.filename, ref.colorCode);
        if (mesh && mesh->tris.empty()) mesh.reset();
        if (mesh) {
            tris += mesh->tris.size();
            edges += mesh->edges.size();
        }
        meshes.push_back(std::move(mesh));
    }
    out.mesh.tris.reserve(tris + src.primitives.size() * 2);
    out.mesh.edges.reserve(edges);
    for (std::size_t i = 0; i < src.parts.size(); ++i) {
        const auto& ref = src.parts[i];
        if (!meshes[i]) {
            out.unresolvedRefs++;
            continue;
        }
        const geom::Mesh& partMesh = *meshes[i];
        out.resolvedRefs++;
        const geom::Mat4 xform = refTransform(ref);
        for (const auto& t : partMesh.tris) {
            geom::Triangle world;
            world.v[0] = xform.transform(t.v[0]);
            world.v[1] = xform.transform(t.v[1]);
            world.v[2] = xform.transform(t.v[2]);
            // Flat-shaded face normal from the post-transform vertices.
            // LDraw `.dat` doesn't ship per-vertex normals; one normal
            // per triangle is sufficient for our top-down lighting pass.
            const geom::Vec3 e1 = world.v[1] - world.v[0];
            const geom::Vec3 e2 = world.v[2] - world.v[0];
            geom::Vec3 fn{
                e1.y * e2.z - e1.z * e2.y,
                e1.z * e2.x - e1.x * e2.z,
                e1.x * e2.y - e1.y * e2.x,
            };
            world.n[0] = fn;
            world.n[1] = fn;
            world.n[2] = fn;
            world.color = t.color;
            out.mesh.tris.push_back(world);
        }
        for (const auto& e : partMesh.edges) {
            geom::Edge world;
            world.v[0] = xform.transform(e.v[0]);
            world.v[1] = xform.transform(e.v[1]);
            world.color = e.color;
            out.mesh.edges.push_back(world);
        }
    }

    // Second pass: inline primitives (Studio exports + hand-authored
    // .ldr snippets sometimes carry geometry directly). Treat them as
    // if they were under an identity transform. Colour 16 ("inherit")
    // is meaningless at top level; treat it as code 7 (light grey)
    // for visibility — same convention LDView and most other tools
    // use when there's no parent ref to inherit from.
    for (const auto& prim : src.primitives) {
        const int resolvedCode = (prim.colorCode == 16) ? 7 : prim.colorCode;
        const QColor finalColor = palette.color(resolvedCode);
        if (prim.kind == 3) {
            geom::Triangle t;
            for (int k = 0; k < 3; ++k) {
                t.v[k] = { prim.v[k][0], prim.v[k][1], prim.v[k][2] };
            }
            t.color = finalColor;
            out.mesh.tris.push_back(t);
        } else if (prim.kind == 4) {
            geom::Vec3 p[4];
            for (int k = 0; k < 4; ++k) {
                p[k] = { prim.v[k][0], prim.v[k][1], prim.v[k][2] };
            }
            geom::Triangle t1, t2;
            t1.v[0] = p[0]; t1.v[1] = p[1]; t1.v[2] = p[2]; t1.color = finalColor;
            t2.v[0] = p[0]; t2.v[1] = p[2]; t2.v[2] = p[3]; t2.color = finalColor;
            out.mesh.tris.push_back(t1);
            out.mesh.tris.push_back(t2);
        }
    }

    convertToMeshFrame(out.mesh);
    out.errors = loader.errors();
    return out;
}

}  // namespace bld::import
