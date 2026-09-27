#include "ImportPipeline.h"

#include "BackgroundTask.h"
#include "MapViewInternal.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../import/ImportConnections.h"
#include "../import/ldd/LDDAssets.h"
#include "../import/ldd/LDDLDrawMapping.h"
#include "../import/ldd/LDDMaterials.h"
#include "../import/ldd/LDDMeshBuilder.h"
#include "../import/ldd/LDDReader.h"
#include "../import/ldraw/LDrawLibrary.h"
#include "../import/ldraw/LDrawMeshBuilder.h"
#include "../import/ldraw/LDrawMeshLoader.h"
#include "../import/ldraw/LDrawPalette.h"
#include "../import/ldraw/LDrawRasterize.h"
#include "../import/ldraw/LDrawReader.h"
#include "../import/mesh/MeshRasterize.h"
#include "../import/studio/StudioReader.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <memory>

namespace bld::ui {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("bld::ui::ImportPipeline", s); }

enum class Format { LDraw, Studio, LDD };

Format formatOf(const QString& path) {
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".io"))) return Format::Studio;
    if (lower.endsWith(QStringLiteral(".lxf")) || lower.endsWith(QStringLiteral(".lxfml"))) return Format::LDD;
    return Format::LDraw;
}

import::RasterizeOptions geometryRasterOptions(int pxPerStud) {
    import::RasterizeOptions opt;
    // Rendered at pxPerStud (32 by default, 4x the map's 8) so studs and
    // prints stay sharp when zoomed; the part XML's <PixelsPerStud> tells
    // the map to scale it back to the right footprint. ssaa 4 = rendered
    // internally at 128 px/stud and smoothly downsampled.
    opt.pxPerStud = pxPerStud;
    opt.marginPx  = 0;
    opt.ssaa      = 4;
    opt.wireframe = true;
    return opt;
}

void fillFromRaster(PreparedPart& out, const import::RasterizeResult& rast) {
    out.sprite      = rast.image;
    out.widthStuds  = static_cast<int>(std::lround(rast.spriteStuds.width()));
    out.heightStuds = static_cast<int>(std::lround(rast.spriteStuds.height()));
}

// Model -> BlueBrick placement -> free connection ends, relative to the
// sprite centre `originStuds`.
void addConnections(PreparedPart& out, const import::LDrawReadResult& ldrawRead,
                    parts::PartsLibrary& parts, QPointF originStuds,
                    const import::LDrawLibrary* ldraw, import::LDrawMeshLoader* loader) {
    auto placed = import::toBlueBrickMap(ldrawRead, &parts, ldraw, loader);
    out.connections = import::externalConnections(*placed, parts, originStuds);
}

// Real LDraw geometry through the user's LDraw library.
PreparedPart fromLDrawGeometry(PreparedPart out, const import::LDrawReadResult& read,
                               const ImportSettings& settings, parts::PartsLibrary& parts,
                               const HeavyRunner& runHeavy) {
    import::LDrawLibrary lib(settings.ldrawLibrary);
    import::LDrawPalette palette;
    palette.loadFromLDConfig(QDir(settings.ldrawLibrary).absoluteFilePath(QStringLiteral("LDConfig.ldr")));
    import::LDrawMeshLoader loader(lib, palette);
    import::BakedModel baked;
    import::RasterizeResult rast;
    const bool finished = runHeavy(tr("Importing %1...").arg(QFileInfo(out.source).fileName()),
        [&](CancelToken& cancel) {
            baked = import::bakeMeshFromLDraw(read, loader, palette);
            if (cancel.requested() || baked.mesh.tris.empty()) return;
            rast = import::rasterizeMeshTopDown(baked.mesh, geometryRasterOptions(settings.pxPerStud));
        });
    if (!finished) { out.cancelled = true; out.error = tr("Import cancelled."); return out; }
    out.warnings = baked.errors;
    out.stats.ldrawResolved = baked.resolvedRefs;
    out.stats.unresolved    = baked.unresolvedRefs;
    if (baked.mesh.tris.empty() || rast.image.isNull()) {
        out.error = tr("The LDraw library at %1 has no geometry for any part in this model.")
                        .arg(settings.ldrawLibrary);
        return out;
    }
    fillFromRaster(out, rast);
    addConnections(out, read, parts, rast.spriteStuds.center(), &lib, &loader);
    return out;
}

// No LDraw library: composite the BlueBrickParts sprites of the parts the
// library knows, at the library's native 8 px/stud. GUI thread only.
PreparedPart fromBlueBrickParts(PreparedPart out, const import::LDrawReadResult& read,
                                parts::PartsLibrary& parts) {
    constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;
    auto model = import::toBlueBrickMap(read, &parts);
    const auto& bricks = static_cast<const core::LayerBrick&>(*model->layers().front()).bricks;
    for (const auto& b : bricks) {
        (parts.metadata(b.partNumber) ? out.stats.ldrawResolved : out.stats.unresolved)++;
    }

    QGraphicsScene scene;
    rendering::SceneBuilder renderer(scene, parts);
    if (out.stats.ldrawResolved > 0) renderer.build(*model);
    // Keep only the brick sprites: SceneBuilder also adds connection
    // dots, hulls and labels according to the user's View settings, and
    // hidden ones still count towards itemsBoundingRect().
    QRectF bounds;
    for (QGraphicsItem* it : scene.items()) {
        if (it->parentItem() || !detail::isBrickItem(it)) {
            it->setVisible(false);
            continue;
        }
        // The pixmap itself, not boundingRect(): smooth-transformed
        // pixmap items pad that by half a pixel on every side.
        if (auto* pm = qgraphicsitem_cast<QGraphicsPixmapItem*>(it)) {
            bounds |= pm->mapRectToScene(QRectF(pm->offset(), pm->pixmap().deviceIndependentSize()));
        } else {
            bounds |= it->sceneBoundingRect();
        }
    }
    if (bounds.isEmpty() && !read.primitives.empty()) {
        // Only inline geometry (e.g. a hand-written .ldr): draw that.
        const QImage prim = import::rasterizeTopDown(read);
        if (!prim.isNull()) {
            auto* item = scene.addPixmap(QPixmap::fromImage(prim));
            item->setOffset(-prim.width() / 2.0, -prim.height() / 2.0);
            bounds = item->sceneBoundingRect();
        }
    }
    if (bounds.isEmpty()) {
        out.error = out.stats.unresolved > 0
            ? tr("None of the %1 parts in this model are in the parts library. "
                 "Set an LDraw library in Preferences to render any part.").arg(out.stats.unresolved)
            : tr("The model is empty.");
        return out;
    }

    // Pad to whole studs around the model's centre, rendered 1:1.
    const QPointF c = bounds.center();
    out.widthStuds  = std::max(1, static_cast<int>(std::ceil(bounds.width()  / kPx - 0.05)));
    out.heightStuds = std::max(1, static_cast<int>(std::ceil(bounds.height() / kPx - 0.05)));
    const QRectF canvas(c.x() - out.widthStuds * kPx / 2.0, c.y() - out.heightStuds * kPx / 2.0,
                        out.widthStuds * kPx, out.heightStuds * kPx);
    out.sprite = QImage(static_cast<int>(canvas.width()), static_cast<int>(canvas.height()),
                        QImage::Format_ARGB32_Premultiplied);
    out.sprite.fill(Qt::transparent);
    {
        QPainter p(&out.sprite);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        scene.render(&p, QRectF(QPointF(0, 0), canvas.size()), canvas);
    }
    out.connections = import::externalConnections(*model, parts, c / kPx);
    return out;
}

// LDD's own geometry from its brick database.
PreparedPart fromLDDGeometry(PreparedPart out, const import::LDrawReadResult& read,
                             const import::LDDAssets& assets, const import::LDDLDrawMapping* mapping,
                             const ImportSettings& settings, parts::PartsLibrary& parts,
                             const HeavyRunner& runHeavy) {
    import::LDDMaterials materials;
    materials.loadFromBytes(assets.read(QStringLiteral("Materials.xml")));
    import::LDDLDrawBakedModel baked;
    import::RasterizeResult rast;
    const bool finished = runHeavy(tr("Importing %1...").arg(QFileInfo(out.source).fileName()),
        [&](CancelToken& cancel) {
            import::LDDMeshBuilder builder;
            builder.setAssets(&assets);
            builder.setMaterials(&materials);
            baked = builder.bake(read);
            if (cancel.requested() || baked.mesh.tris.empty()) return;
            rast = import::rasterizeMeshTopDown(baked.mesh, geometryRasterOptions(settings.pxPerStud));
        });
    if (!finished) { out.cancelled = true; out.error = tr("Import cancelled."); return out; }
    out.warnings = baked.errors;
    out.stats.lddRendered = baked.rendered;
    out.stats.unresolved  = baked.skipped;
    if (baked.mesh.tris.empty() || rast.image.isNull()) {
        out.error = tr("LDD's brick database (%1) has no geometry for this model.").arg(assets.source());
        return out;
    }
    fillFromRaster(out, rast);
    // Snap points need LDraw part numbers: go through ldraw.xml. The LDD
    // mesh frame (+Y up, image y = LDD z) and the converted LDraw model's
    // top-down frame (y = -LDraw z = LDD z) line up.
    if (mapping) {
        std::unique_ptr<import::LDrawLibrary> lib;
        std::unique_ptr<import::LDrawPalette> palette;
        std::unique_ptr<import::LDrawMeshLoader> loader;
        if (!settings.ldrawLibrary.isEmpty()) {
            lib = std::make_unique<import::LDrawLibrary>(settings.ldrawLibrary);
            if (lib->looksValid()) {
                palette = std::make_unique<import::LDrawPalette>();
                loader = std::make_unique<import::LDrawMeshLoader>(*lib, *palette);
            } else {
                lib.reset();
            }
        }
        addConnections(out, mapping->toLDraw(read), parts, rast.spriteStuds.center(),
                       lib.get(), loader.get());
    }
    return out;
}

}  // namespace

bool isImportableFile(const QString& path) {
    static const QStringList exts{ QStringLiteral("ldr"), QStringLiteral("dat"), QStringLiteral("mpd"),
                                   QStringLiteral("io"), QStringLiteral("lxf"), QStringLiteral("lxfml") };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

PreparedPart prepareImport(const QString& path, const ImportSettings& settings,
                           parts::PartsLibrary& parts, const HeavyRunner& runHeavy) {
    PreparedPart out;
    out.source = path;
    const Format format = formatOf(path);
    out.kindLabel = format == Format::Studio ? tr("Studio import")
                  : format == Format::LDD    ? tr("LDD import")
                                             : tr("LDraw import");

    import::LDrawReadResult read = format == Format::Studio ? import::readStudioIo(path)
                                 : format == Format::LDD    ? import::readLDD(path)
                                                            : import::readLDraw(path);
    if (!read.ok) {
        out.error = tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), read.error);
        return out;
    }

    if (format == Format::LDD) {
        import::LDDAssets assets;
        assets.open(settings.lddPath);
        import::LDDLDrawMapping mapping;
        const QString xml = !settings.lddLdrawXml.isEmpty() ? settings.lddLdrawXml
                                                             : assets.ldrawXmlPath();
        const bool haveMapping = !xml.isEmpty() && mapping.loadFromFile(xml);
        if (assets.isOpen()) {
            return fromLDDGeometry(out, read, assets, haveMapping ? &mapping : nullptr,
                                   settings, parts, runHeavy);
        }
        // No brick database, but ldraw.xml: treat it as the LDraw model
        // LDD itself would export.
        if (haveMapping) read = mapping.toLDraw(read);
    }

    // Studio files use Studio's own LDraw library when one is set (it has
    // Studio-only parts), else the regular one.
    ImportSettings geometry = settings;
    if (format == Format::Studio && import::LDrawLibrary(settings.studioLibrary).looksValid()) {
        geometry.ldrawLibrary = settings.studioLibrary;
    }
    if (!read.lddAxes && import::LDrawLibrary(geometry.ldrawLibrary).looksValid()) {
        return fromLDrawGeometry(out, read, geometry, parts, runHeavy);
    }
    return fromBlueBrickParts(out, read, parts);
}

void rotatePart(PreparedPart& part, int quarterTurns) {
    const int turns = ((quarterTurns % 4) + 4) % 4;
    if (turns == 0 || part.sprite.isNull()) return;
    part.sprite = part.sprite.transformed(QTransform().rotate(90.0 * turns));
    if (turns % 2) std::swap(part.widthStuds, part.heightStuds);
    for (auto& c : part.connections) {
        for (int i = 0; i < turns; ++i) {
            // Clockwise quarter turn in y-down coordinates.
            const double x = c.xStuds;
            c.xStuds = -c.yStuds;
            c.yStuds = x;
        }
        c.angleDeg = std::remainder(c.angleDeg + 90.0 * turns, 360.0);
    }
    for (auto& p : part.droppedConnections)
        for (int i = 0; i < turns; ++i) p = QPointF(-p.y(), p.x());
    part.quarterTurns = (part.quarterTurns + turns) % 4;
}

void applyImportEdits(PreparedPart& part, int quarterTurns, const QVector<QPointF>& dropped) {
    rotatePart(part, quarterTurns);
    for (const QPointF& d : dropped) {
        for (int i = 0; i < part.connections.size(); ++i) {
            const auto& c = part.connections[i];
            if (QLineF(QPointF(c.xStuds, c.yStuds), d).length() <= 0.5) {
                part.droppedConnections << QPointF(c.xStuds, c.yStuds);
                part.connections.removeAt(i);
                break;
            }
        }
    }
}

QString writeImportedPart(const PreparedPart& part, const QString& name,
                          const QString& destDir, const QString& author,
                          bool replaceExisting, QString* error) {
    import::ImportSource source;
    source.path = QFileInfo(part.source).absoluteFilePath();
    source.modified = QFileInfo(part.source).lastModified();
    source.quarterTurns = part.quarterTurns;
    source.droppedConnections = part.droppedConnections;
    return import::writeImportedModelAsLibraryPart(
        name, part.sprite, part.widthStuds, part.heightStuds, destDir, author,
        part.connections, error, replaceExisting, part.source.isEmpty() ? nullptr : &source);
}

}  // namespace bld::ui
