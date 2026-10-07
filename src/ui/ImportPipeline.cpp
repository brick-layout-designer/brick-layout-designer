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
#include <QRegularExpression>
#include <QPainter>
#include <QSizeF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <memory>

namespace bld::ui {

namespace {

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call Text::tr().
struct Text { Q_DECLARE_TR_FUNCTIONS(bld::ui::ImportPipeline) };

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
    out.snapMargin  = rast.snapMargin;
    out.contentStuds = rast.meshBoundsXZ.translated(-rast.spriteStuds.topLeft());
    out.baseOnGrid  = rast.baseOnGrid;
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
    // Parts that came with the model (a Studio file's CustomParts).
    lib.setOverlayDirs(read.extraPartDirs);
    import::LDrawPalette palette;
    palette.loadFromLDConfig(QDir(settings.ldrawLibrary).absoluteFilePath(QStringLiteral("LDConfig.ldr")));
    import::LDrawMeshLoader loader(lib, palette);
    import::BakedModel baked;
    import::RasterizeResult rast;
    const bool finished = runHeavy(Text::tr("Importing %1...").arg(QFileInfo(out.source).fileName()),
        [&](CancelToken& cancel) {
            baked = import::bakeMeshFromLDraw(read, loader, palette);
            if (cancel.requested() || baked.mesh.tris.empty()) return;
            rast = import::rasterizeMeshTopDown(baked.mesh, geometryRasterOptions(settings.pxPerStud));
        });
    if (!finished) { out.cancelled = true; out.error = Text::tr("Import cancelled."); return out; }
    out.warnings = read.warnings + baked.errors;
    out.stats.ldrawResolved = baked.resolvedRefs;
    out.stats.unresolved    = baked.unresolvedRefs;
    if (baked.mesh.tris.empty() || rast.image.isNull()) {
        out.error = Text::tr("The LDraw library at %1 has no geometry for any part in this model.")
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
            ? Text::tr("None of the %1 parts in this model are in the parts library. "
                 "Set an LDraw library in Preferences to render any part.").arg(out.stats.unresolved)
            : Text::tr("The model is empty.");
        return out;
    }

    // Pad to whole studs around the model's centre, rendered 1:1.
    const QPointF c = bounds.center();
    out.widthStuds  = std::max(1, static_cast<int>(std::ceil(bounds.width()  / kPx - 0.05)));
    out.heightStuds = std::max(1, static_cast<int>(std::ceil(bounds.height() / kPx - 0.05)));
    out.contentStuds = QRectF((out.widthStuds - bounds.width() / kPx) / 2.0, (out.heightStuds - bounds.height() / kPx) / 2.0,
                              bounds.width() / kPx, bounds.height() / kPx);
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
    const bool finished = runHeavy(Text::tr("Importing %1...").arg(QFileInfo(out.source).fileName()),
        [&](CancelToken& cancel) {
            import::LDDMeshBuilder builder;
            builder.setAssets(&assets);
            builder.setMaterials(&materials);
            baked = builder.bake(read);
            if (cancel.requested() || baked.mesh.tris.empty()) return;
            rast = import::rasterizeMeshTopDown(baked.mesh, geometryRasterOptions(settings.pxPerStud));
        });
    if (!finished) { out.cancelled = true; out.error = Text::tr("Import cancelled."); return out; }
    out.warnings = baked.errors;
    out.stats.lddRendered = baked.rendered;
    out.stats.unresolved  = baked.skipped;
    if (baked.mesh.tris.empty() || rast.image.isNull()) {
        out.error = Text::tr("LDD's brick database (%1) has no geometry for this model.").arg(assets.source());
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

PreparedPart prepareImport(const QString& path, const ImportSettings& settings,
                           parts::PartsLibrary& parts, const HeavyRunner& runHeavy) {
    PreparedPart out;
    out.source = path;
    const Format format = formatOf(path);
    out.kindLabel = format == Format::Studio ? Text::tr("Studio import")
                  : format == Format::LDD    ? Text::tr("LDD import")
                                             : Text::tr("LDraw import");

    import::LDrawReadResult read = format == Format::Studio ? import::readStudioIo(path)
                                 : format == Format::LDD    ? import::readLDD(path)
                                                            : import::readLDraw(path);
    if (!read.ok) {
        out.error = Text::tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), read.error);
        return out;
    }
    out.title = read.title.trimmed();

    if (format == Format::LDD) {
        import::LDDAssets assets;
        assets.open(settings.lddPath);
        import::LDDLDrawMapping mapping;
        const QString xml = !settings.lddLdrawXml.isEmpty() ? settings.lddLdrawXml
                                                             : assets.ldrawXmlPath();
        const bool haveMapping = !xml.isEmpty() && mapping.loadFromFile(xml);
        if (assets.isOpen()) {
            // Even without an ldraw.xml the built-in track table finds snap points.
            return fromLDDGeometry(out, read, assets, &mapping,
                                   settings, parts, runHeavy);
        }
        // No brick database: the LDraw model LDD itself would export, through
        // ldraw.xml. Without one, only the built-in track table maps parts,
        // so compose from the parts library (track then still snaps).
        read = mapping.toLDraw(read);
        if (!haveMapping) return fromBlueBrickParts(out, read, parts);
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

PreparedPart alignPart(const PreparedPart& part, ImportAlign align, QPointF nudgeStuds) {
    PreparedPart out = part;
    if (part.sprite.isNull() || part.widthStuds <= 0 || part.heightStuds <= 0 || part.contentStuds.isEmpty()) return out;
    // Where the model's bounds start, before the nudge.
    QPointF shift;
    constexpr double kSlack = 0.05;
    if (align == ImportAlign::BoundingBox || (align == ImportAlign::BottomLayer && !part.baseOnGrid)) {
        const QSizeF c = part.contentStuds.size();
        const QPointF centred((std::max(1.0, std::ceil(c.width() - kSlack)) - c.width()) / 2.0,
                              (std::max(1.0, std::ceil(c.height() - kSlack)) - c.height()) / 2.0);
        shift = centred - part.contentStuds.topLeft();
    }
    shift += nudgeStuds;
    if (shift.isNull()) return out;

    // The old sprite, moved by `shift`, re-cut to whole studs around what it
    // shows. Whole-stud moves change nothing: the grid is the same.
    const QRectF content = part.contentStuds.translated(shift);
    const QPointF origin(std::floor(content.left() + kSlack), std::floor(content.top() + kSlack));  // new top-left
    out.widthStuds = std::max(1, static_cast<int>(std::ceil(content.right() - kSlack) - origin.x()));
    out.heightStuds = std::max(1, static_cast<int>(std::ceil(content.bottom() - kSlack) - origin.y()));
    const double px = static_cast<double>(part.sprite.width()) / part.widthStuds;
    QImage img(qRound(out.widthStuds * px), qRound(out.heightStuds * px), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QPointF((shift.x() - origin.x()) * px, (shift.y() - origin.y()) * px), part.sprite);
    }
    out.sprite = img;
    out.contentStuds = content.translated(-origin);
    // Connections are relative to the sprite centre.
    const QPointF oldCentre(part.widthStuds / 2.0, part.heightStuds / 2.0);
    const QPointF newCentre = origin + QPointF(out.widthStuds / 2.0, out.heightStuds / 2.0);
    const QPointF d = oldCentre + shift - newCentre;
    for (auto& c : out.connections) {
        c.xStuds += d.x();
        c.yStuds += d.y();
    }
    for (auto& p : out.droppedConnections) p += d;
    // The base (inside the old margin) moves too; the margin stays whole studs.
    if (!part.snapMargin.isNull()) {
        const QRectF base = QRectF(part.snapMargin.left(), part.snapMargin.top(),
                                   part.widthStuds - part.snapMargin.left() - part.snapMargin.right(),
                                   part.heightStuds - part.snapMargin.top() - part.snapMargin.bottom())
                                .translated(shift - origin);
        out.snapMargin = QMargins(std::max(0, static_cast<int>(std::floor(base.left() + kSlack))),
                                  std::max(0, static_cast<int>(std::floor(base.top() + kSlack))),
                                  std::max(0, static_cast<int>(std::floor(out.widthStuds - base.right() + kSlack))),
                                  std::max(0, static_cast<int>(std::floor(out.heightStuds - base.bottom() + kSlack))));
    }
    return out;
}

void rotatePart(PreparedPart& part, int quarterTurns) {
    const int turns = ((quarterTurns % 4) + 4) % 4;
    if (turns == 0 || part.sprite.isNull()) return;
    part.sprite = part.sprite.transformed(QTransform().rotate(90.0 * turns));
    for (int i = 0; i < turns; ++i) {
        // Clockwise in y-down stud coords: (x, y) -> (H - y, x).
        const QRectF c = part.contentStuds;
        const double h = (i % 2 == 0) ? part.heightStuds : part.widthStuds;
        part.contentStuds = QRectF(h - c.bottom(), c.left(), c.height(), c.width());
    }
    if (turns % 2) std::swap(part.widthStuds, part.heightStuds);
    for (int i = 0; i < turns; ++i) {
        // Clockwise: the bottom edge becomes the left one, the left the top.
        const QMargins m = part.snapMargin;
        part.snapMargin = QMargins(m.bottom(), m.left(), m.top(), m.right());
    }
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
                          bool replaceExisting, QString* error, const QString& keyName) {
    import::ImportSource source;
    source.displayName = name.trimmed();
    static const QRegularExpression modelExt(QStringLiteral("\\.(ldr|dat|mpd|io|lxf|lxfml)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    source.displayName.remove(modelExt);
    source.format = import::importFormatOf(part.source);
    source.path = QFileInfo(part.source).absoluteFilePath();
    source.modified = QFileInfo(part.source).lastModified();
    source.quarterTurns = part.quarterTurns;
    source.droppedConnections = part.droppedConnections;
    return import::writeImportedModelAsLibraryPart(
        keyName.isEmpty() ? name : keyName, part.sprite, part.widthStuds, part.heightStuds, destDir, author,
        part.connections, error, replaceExisting, part.source.isEmpty() ? nullptr : &source,
        part.snapMargin);
}

}  // namespace bld::ui
