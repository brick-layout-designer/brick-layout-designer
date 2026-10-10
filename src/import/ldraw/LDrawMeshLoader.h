#pragma once

#include "../../geom/Mesh.h"
#include "LDrawLibrary.h"
#include "LDrawPalette.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>

namespace bld::import {

// Walk a .dat file's lines and recursively bake every subfile reference
// into a flat triangle list — the geom::Mesh the rasterizer wants.
//
// Behaviour:
//   * Type-1 (subfile ref): looks up via LDrawLibrary, recurses with
//     the composed transform and inherited color.
//   * Type-3 (triangle): emits one triangle.
//   * Type-4 (quad): emits two triangles split along v0..v2 diagonal.
//   * Type-2 / 5 (lines / conditional lines): ignored.
//   * Comments / BFC / META: ignored.
//
// Color resolution mirrors the LDraw spec:
//   * 16 = "use parent's current color" — passed in by the caller
//     when recursing.
//   * 24 = "use parent's edge color" — irrelevant to filled tris.
//   * Any other code = absolute lookup against the palette.
//
// Loaded files are cached by absolute path so a part that pulls in
// `box.dat` 50 times only parses + bakes that file once. The cache
// stores a *pre-resolved* mesh in the file's own local coords so we
// can re-instance it under different parent transforms without
// re-parsing.
class LDrawMeshLoader {
public:
    LDrawMeshLoader(const LDrawLibrary& library, const LDrawPalette& palette);

    // Load a top-level part by .dat reference (e.g. "3001.dat") into
    // a Mesh resolved to part-local coords. Returns an empty mesh if
    // the file cannot be resolved or is malformed beyond recovery.
    // `topColor` controls how a top-level color-16 (rare in real
    // parts but valid) gets resolved; default code 16 leaves them as
    // light grey via the palette's bundled fallback.
    geom::Mesh loadPart(const QString& datRef, int topColor = 16);
    // The same without the copy: the cached mesh, or null.
    std::shared_ptr<const geom::Mesh> loadPartShared(const QString& datRef, int topColor = 16);

    // Errors / warnings collected while loading. Cleared by clearErrors().
    const QStringList& errors() const { return errors_; }
    void clearErrors() { errors_.clear(); }

    // Reset the parsed-file cache. Call after switching libraries.
    void clearCache() { cache_.clear(); }

private:
    // A `.dat` parsed into its raw line records (no transforms applied
    // yet). Subfile refs carry their color + 4x4 transform + filename;
    // raw primitives carry the LDraw color code + 3 vertices in part-
    // local coords. Color resolution against the live palette is
    // deferred to the bake pass since the inheritance chain depends on
    // who the parent caller is.
    struct ParsedDat {
        struct SubRef {
            int      color = 16;
            geom::Mat4 transform;
            QString  child;            // resolver input ".dat" name
        };
        struct RawTri {
            int       color = 16;
            geom::Vec3 v[3];
        };
        // LDraw type-2 line: a wireframe edge between two points. We
        // load these so the rasterizer can stroke them as feature
        // edges over the filled triangles — that's how stud outlines,
        // brick seams, and embossed-print silhouettes show up on a
        // top-down sprite without needing per-pixel depth tricks.
        struct RawEdge {
            int       color = 24;        // LDraw "edge" code
            geom::Vec3 v[2];
        };
        std::vector<SubRef>  subrefs;
        std::vector<RawTri>  primitives;  // tris already split from quads
        std::vector<RawEdge> edges;
    };
    const ParsedDat* parse(const QString& absolutePath);
    void appendBaked(geom::Mesh& out,
                     const ParsedDat& dat,
                     const geom::Mat4& parentXform,
                     int                parentColor);

    const LDrawLibrary& lib_;
    const LDrawPalette& palette_;
    QHash<QString, ParsedDat> cache_;  // keyed on absolute path
    // Memoised final-mesh cache keyed by (absolute path, top color
    // code). Same part in the same color gets baked once even if a
    // model references it thousands of times.
    QHash<QPair<QString, int>, std::shared_ptr<const geom::Mesh>> bakedCache_;
    QStringList errors_;
};

}  // namespace bld::import
