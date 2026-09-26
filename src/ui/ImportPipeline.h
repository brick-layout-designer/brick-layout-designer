#pragma once

#include "../import/ImportToPart.h"

#include <QImage>
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
struct PreparedPart {
    QString source;
    QString kindLabel;                              // "LDraw import", ...
    QImage  sprite;
    int     widthStuds  = 0;
    int     heightStuds = 0;
    QVector<import::ImportedConnection> connections;  // relative to sprite centre
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

// Rotate the part clockwise by quarterTurns x 90°: sprite, footprint and
// connection points together.
void rotatePart(PreparedPart& part, int quarterTurns);

// Write `part` as <destDir>/<name>.{xml,png,gif}. With replaceExisting an
// existing part of that name is overwritten; otherwise a -2, -3 ... suffix
// keeps both. Returns the part key, or empty with *error set.
QString writeImportedPart(const PreparedPart& part, const QString& name,
                          const QString& destDir, const QString& author,
                          bool replaceExisting, QString* error);

// True for the file extensions prepareImport understands.
bool isImportableFile(const QString& path);

}  // namespace bld::ui
