#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::import {

// One type-1 LDraw subfile reference parsed out of a .ldr / .dat / .mpd file.
// LDraw axes: -Y is up (Y points down), Z is the "forward" axis. We store the raw fields as
// read; conversion to BlueBrick coords happens in toBlueBrickMap().
struct LDrawPartRef {
    int     colorCode = 0;
    double  x = 0, y = 0, z = 0;        // LDU
    double  m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };  // 3x3 rotation/scale matrix
    QString filename;                    // e.g. "3001.dat"
    // Other files known to be the same part (e.g. from LDD's ldraw.xml,
    // which lists both "2865.dat" and "74746.dat" for the 9V straight).
    // Only used to find the part in a parts library.
    QStringList aliases;
};

// Inline geometry — line (type 2), triangle (type 3), or quad (type 4).
// Captured so files that inline their geometry (Studio exports, hand-
// authored .ldr snippets) can be rasterized directly when no library
// part resolves. Vertex coords are LDU, same axes as type 1 refs.
// Type 5 (conditional line) is intentionally NOT captured — it only
// draws when adjacent surfaces are separated by an edge, which is
// render-pipeline detail irrelevant to a flat top-down sprite.
struct LDrawPrimitive {
    int    kind = 0;           // 2 = line, 3 = tri, 4 = quad
    int    colorCode = 0;
    double v[4][3] = { {0} };  // up to 4 vertices (x, y, z) each
};

struct LDrawReadResult {
    bool ok = false;
    QString error;
    std::vector<LDrawPartRef>    parts;
    std::vector<LDrawPrimitive>  primitives;
    QString title;  // first comment line (line 0 after leading "0")
    QString author; // the main model's "0 Author:" line, if it has one
    // Set by readLDD: refs keep LDD's native axes (+Y up) rather than
    // LDraw's, so toBlueBrickMap must not apply LDraw's y = -z mapping.
    bool lddAxes = false;
    // Folders searched before the LDraw library, e.g. a Studio file's own
    // CustomParts, extracted for this import. `extracted` keeps them alive.
    QStringList extraPartDirs;
    // Problems that didn't stop the read (a custom part that couldn't be
    // unpacked, ...), in words for the user.
    QStringList warnings;
    std::shared_ptr<const void> extracted;
};

// Parse an LDraw text file: line-1 references, line-2/3/4 geometry and the
// title (first line-0 comment). Library parts are NOT resolved — callers map
// .dat names to part numbers separately. Multi-part files (.mpd, Studio's
// model.ldr) are flattened: the first "0 FILE" block is the model, and its
// references to the other blocks are replaced by those blocks' contents,
// placed and coloured. Unreferenced blocks are dropped.
LDrawReadResult readLDraw(const QString& path);

class LDrawLibrary;
class LDrawMeshLoader;

// Convert a parsed LDraw model into a brand-new core::Map with one brick
// layer, placing parts exactly as BlueBrick's own LDraw loader does:
//   - 20 LDU = 1 stud.
//   - Top-down view: x stays, y = -z (LDraw is -Y up).
//   - Orientation = atan2(m[2], m[0]), the rotation around Y.
//   - Part number = file name without directory / ".dat", upper-cased,
//     suffixed with ".<colour>".
// With `lib`, each reference is resolved against the parts library
// (exact colour, any colour, then LDraw's earlier numbers for renumbered
// parts via `ldraw`), the brick takes the library's part number, and the
// part's <LDraw> Angle/Translation remap is applied so the brick's
// displayArea centre is the library image's centre.
//
// With `geometry`, each resolved part's remapped centre is checked
// against the centre of its real LDraw geometry. BlueBrickParts' remaps
// were authored against older LDraw files and some parts have since been
// re-origined (the 9V switch 2861c01 -> 75542-f1 moved its origin from
// the points end to the middle); when the two disagree by more than a
// quarter of the part's size the geometry centre is used instead.
//
// Only pieces present in the parts library render. Use
// `bakeMeshFromLDraw` (LDrawMeshBuilder.h) to render real LDraw geometry.
std::unique_ptr<core::Map> toBlueBrickMap(const LDrawReadResult& src,
                                          const parts::PartsLibrary* lib = nullptr,
                                          const LDrawLibrary* ldraw = nullptr,
                                          LDrawMeshLoader* geometry = nullptr);

}
