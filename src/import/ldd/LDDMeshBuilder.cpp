#include "LDDMeshBuilder.h"

#include "LDDAssets.h"
#include "LDDGeomReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace bld::import {

namespace {

QString stripDesignDecorations(QString s) {
    // LDDReader produces "<designID>.dat" or "<designID>.<matId>.dat".
    if (s.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive)) s.chop(4);
    const int dot = s.indexOf(QChar('.'));
    if (dot > 0) s = s.left(dot);
    return s;
}

// LDD/LU place .g files in Primitives/LOD0/. Some bricks have multi-
// mesh splits as <id>.g1, <id>.g2 — we only ask for the base .g for
// now; LU's brickprimitives uses the same base name with an LOD
// suffix in the filename instead of multi-mesh splitting.
QString primitivePath(const QString& designId) {
    return QStringLiteral("Primitives/LOD0/%1.g").arg(designId);
}

geom::Mat4 ldrawTransformOf(const LDrawPartRef& ref) {
    return geom::Mat4::fromTranslationAndRotation(
        geom::Vec3{ ref.x, ref.y, ref.z },
        ref.m[0], ref.m[1], ref.m[2],
        ref.m[3], ref.m[4], ref.m[5],
        ref.m[6], ref.m[7], ref.m[8]);
}

// Rotation-only application to a normal: drop the translation column,
// no length normalisation here (the rasterizer normalises before use,
// and this saves a sqrt per vertex on huge models).
geom::Vec3 rotateNormal(const geom::Mat4& m, geom::Vec3 n) {
    return {
        m.m[0][0]*n.x + m.m[0][1]*n.y + m.m[0][2]*n.z,
        m.m[1][0]*n.x + m.m[1][1]*n.y + m.m[1][2]*n.z,
        m.m[2][0]*n.x + m.m[2][1]*n.y + m.m[2][2]*n.z,
    };
}

}  // namespace

QByteArray LDDMeshBuilder::fetchPart(const QString& designId) {
    return assets_ ? assets_->read(primitivePath(designId)) : QByteArray();
}

LDDLDrawBakedModel LDDMeshBuilder::bake(const LDrawReadResult& read) {
    LDDLDrawBakedModel out;

    for (const auto& ref : read.parts) {
        const QString designId = stripDesignDecorations(ref.filename);
        // If we have an LDraw mapping for this designID, the LDraw
        // pipeline already handled (or will handle) it. Skip here.
        if (mapping_ && !mapping_->partFor(designId).isEmpty()) continue;

        const QByteArray bytes = fetchPart(designId);
        if (bytes.isEmpty()) {
            out.errors.append(QStringLiteral("missing .g for designID %1").arg(designId));
            ++out.skipped;
            continue;
        }
        const auto geomResult = readLDDGeom(bytes);
        if (!geomResult.ok) {
            out.errors.append(QStringLiteral("bad .g for %1: %2")
                                  .arg(designId, geomResult.error));
            ++out.skipped;
            continue;
        }

        // Resolve the part's colour: LDD materialID lives on the
        // LDrawPartRef as colorCode (LDDReader copies it through).
        QColor partColor = QColor::fromRgb(220, 220, 220);  // grey fallback
        if (materials_) {
            const QColor c = materials_->color(ref.colorCode);
            if (c.isValid()) partColor = c;
        }

        const geom::Mat4 xform = ldrawTransformOf(ref);
        for (const auto& tri : geomResult.mesh.tris) {
            geom::Triangle baked;
            for (int k = 0; k < 3; ++k) {
                baked.v[k] = xform.transform(tri.v[k]);
                baked.n[k] = rotateNormal(xform, tri.n[k]);
            }
            baked.color = partColor;
            out.mesh.tris.push_back(baked);
        }
        // Carry the synthesized stud-rim / top-edge wireframe through
        // the same transform so it lands at the same world position
        // as the triangles.
        out.mesh.edges.reserve(out.mesh.edges.size() + geomResult.mesh.edges.size());
        for (const auto& e : geomResult.mesh.edges) {
            geom::Edge baked;
            baked.v[0] = xform.transform(e.v[0]);
            baked.v[1] = xform.transform(e.v[1]);
            baked.color = e.color;
            out.mesh.edges.push_back(baked);
        }
        ++out.rendered;
    }
    return out;
}

}  // namespace bld::import
