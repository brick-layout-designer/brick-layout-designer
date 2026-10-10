#pragma once

#include "../import/ImportToPart.h"

#include <QImage>
#include <QMargins>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

struct CancelToken;

// Counts the importer reports, shown in the preview / batch summary.
struct ImportStats {
    int ldrawResolved = 0;  // refs rendered from LDraw (or BlueBrickParts) geometry
    int lddRendered   = 0;  // refs rendered from LDD's own .g geometry
    int unresolved    = 0;  // refs nothing could render
};

// An imported model turned into a library-part-to-be: sprite, footprint
// and snap points. Nothing is written until writeImportedPart().
// How the import dialog lines the model up with the stud grid.
enum class ImportAlign { Automatic, BottomLayer, BoundingBox };

struct PreparedPart {
    QString source;
    QString title;                                  // the model's own name, if it has one
    QString kindLabel;                              // "LDraw import", ...
    QImage  sprite;
    int     widthStuds  = 0;
    int     heightStuds = 0;
    // Whole studs between the sprite's edges and the model's bottom layer
    // (its overhangs), written as the part's <SnapMargin>.
    QMargins snapMargin;
    // The model's own bounds, in studs from the sprite's top-left, and
    // whether the automatic layout put its bottom layer on the stud grid:
    // what alignPart() works from.
    QRectF  contentStuds;
    bool    baseOnGrid = false;
    QVector<import::ImportedConnection> connections;  // relative to sprite centre
    int quarterTurns = 0;                           // clockwise turns applied (rotatePart)
    QVector<QPointF> droppedConnections;            // removed in the preview (current frame)
    // The stud alignment chosen in the preview (alignPart), kept for
    // Re-import from Source, and how far it moved the connection points.
    ImportAlign align = ImportAlign::Automatic;
    QPointF     nudgeStuds;
    QPointF     alignShift;
    ImportStats stats;
    QStringList warnings;
    QString     error;                             // non-empty: nothing to import
    bool        cancelled = false;                 // the user cancelled the bake
    bool ok() const { return error.isEmpty() && !sprite.isNull(); }
};

struct ImportSettings {
    QString ldrawLibrary;   // Preferences → LDraw library root (may be empty)
    QString studioLibrary;  // Preferences → Studio's LDraw library; used for .io files
    QString lddPath;        // Preferences → LDD install / data folder (may be empty)
    QString lddLdrawXml;    // Preferences → newer ldraw.xml overriding LDD's own
    int     pxPerStud = 32; // resolution of geometry-rendered sprites
};

// Runs `work` somewhere that keeps the UI alive (a worker thread) and
// returns false if the user cancelled. Only geometry baking goes through
// it; anything touching QPixmap stays on the calling (GUI) thread.
using HeavyRunner = std::function<bool(const QString& label,
                                       const std::function<void(CancelToken&)>& work)>;

// Turn an LDraw (.ldr/.dat/.mpd), Studio (.io) or LDD (.lxf/.lxfml) file
// into a PreparedPart. Must be called on the GUI thread.
//   * With an LDraw library: real LDraw geometry at settings.pxPerStud.
//   * LDD with LDD's brick database: LDD's own geometry; without the
//     database but with its ldraw.xml, the model is converted to LDraw.
//   * Otherwise: composited from BlueBrickParts sprites at 8 px/stud.
// Snap points come from pieces BlueBrickParts knows (track, road, ...).
PreparedPart prepareImport(const QString& path, const ImportSettings& settings,
                           parts::PartsLibrary& parts, const HeavyRunner& runHeavy);

// The part laid out again over the stud grid: `align` picks the starting
// place (Automatic: as prepared, which puts a flat whole-stud bottom layer
// on the grid; BoundingBox: the model's bounds centred in whole studs),
// then the model moves `nudgeStuds` (down to 1/16 stud) against the grid. The
// sprite is re-cut to whole studs around it; connection points and the
// <SnapMargin> move with it.
PreparedPart alignPart(const PreparedPart& part, ImportAlign align, QPointF nudgeStuds);

// Rotate the part clockwise by quarterTurns x 90°: sprite, footprint and
// connection points together.
void rotatePart(PreparedPart& part, int quarterTurns);

// Repeat an earlier import's preview edits on a fresh preparation of the
// same model: its rotation, and dropping connections within half a stud
// of the ones removed then.
void applyImportEdits(PreparedPart& part, int quarterTurns, const QVector<QPointF>& dropped);

// Write `part` as <destDir>/<name>.{xml,png,gif}. With replaceExisting an
// existing part of that name is overwritten; otherwise a -2, -3 ... suffix
// keeps both. The source and preview edits are recorded for Re-import from
// Source. Returns the part key, or empty with *error set.
// `name` is what people see (the part's description); the part number is
// made from it. `keyName`, when given, is the part number to write instead
// (a re-import keeps its part).
QString writeImportedPart(const PreparedPart& part, const QString& name,
                          const QString& destDir, const QString& author,
                          bool replaceExisting, QString* error, const QString& keyName = {});

}  // namespace bld::ui
