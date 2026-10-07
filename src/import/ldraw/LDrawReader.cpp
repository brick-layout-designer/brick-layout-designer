#include "LDrawReader.h"

#include "../../core/Brick.h"
#include "../../core/LayerBrick.h"
#include "../../core/Map.h"
#include "../../parts/BrickPlacement.h"
#include "../../parts/PartsLibrary.h"
#include "LDrawLibrary.h"
#include "LDrawMeshLoader.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <map>
#include <cmath>
#include <optional>

namespace bld::import {

namespace {

constexpr double kLduPerStud = 20.0;

// Extract rotation around Y axis (up) from the 3x3 matrix. For flat layouts
// this is the dominant rotation; tilted parts will approximate here.
// Matrix layout:   [ m0 m1 m2 ]
//                  [ m3 m4 m5 ]
//                  [ m6 m7 m8 ]
// Pure Y rotation: m0 =  cos θ, m2 = sin θ,  m6 = -sin θ, m8 = cos θ.
double yRotationDegrees(const double m[9]) {
    return qRadiansToDegrees(std::atan2(m[2], m[0]));
}

QString partNumberFromFilename(const QString& f) {
    QString s = f.trimmed();
    // Strip any directory prefix ("s/", "48/", etc.).
    const int slash = s.lastIndexOf(QLatin1Char('/'));
    const int bslsh = s.lastIndexOf(QLatin1Char('\\'));
    const int cut = std::max(slash, bslsh);
    if (cut >= 0) s = s.mid(cut + 1);
    // Strip ".dat" (any case).
    if (s.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive)) s.chop(4);
    return s.toUpper();
}

}

namespace {

// One "0 FILE" block of a multi-part file (or the whole file).
struct LDrawBlock {
    std::vector<LDrawPartRef>   parts;
    std::vector<LDrawPrimitive> primitives;
};

// Caps for flattening submodels: a file that nests a submodel many times at
// every level would otherwise multiply out without bound.
constexpr int kMaxSubmodelDepth = 32;
constexpr std::size_t kMaxFlattenedItems = 1'000'000;

struct Placement {
    double m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    double t[3] = { 0, 0, 0 };
    int color = 16;

    void apply(const double v[3], double out[3]) const {
        for (std::size_t r = 0; r < 3; ++r) out[r] = t[r] + m[r * 3] * v[0] + m[r * 3 + 1] * v[1] + m[r * 3 + 2] * v[2];
    }
    int colorOf(int c) const { return c == 16 ? color : c; }
    // This placement followed by a reference inside the placed block.
    Placement then(const LDrawPartRef& ref) const {
        Placement p;
        const double local[3] = { ref.x, ref.y, ref.z };
        apply(local, p.t);
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t c = 0; c < 3; ++c)
                p.m[r * 3 + c] = m[r * 3] * ref.m[c] + m[r * 3 + 1] * ref.m[3 + c] + m[r * 3 + 2] * ref.m[6 + c];
        p.color = colorOf(ref.colorCode);
        return p;
    }
};

class Flattener {
public:
    Flattener(const std::map<QString, LDrawBlock>& blocks, LDrawReadResult& out) : blocks_(blocks), out_(out) {}

    // False when the model is too big or nests too deep.
    bool add(const LDrawBlock& block, const Placement& at, int depth, QStringList& stack) {
        for (const LDrawPartRef& ref : block.parts) {
            const QString key = ref.filename.trimmed().toLower();
            const auto sub = blocks_.find(key);
            if (sub != blocks_.end() && !stack.contains(key)) {
                if (depth >= kMaxSubmodelDepth) return false;
                stack << key;
                const bool ok = add(sub->second, at.then(ref), depth + 1, stack);
                stack.removeLast();
                if (!ok) return false;
                continue;
            }
            if (++items_ > kMaxFlattenedItems) return false;
            const Placement p = at.then(ref);
            LDrawPartRef placed = ref;
            placed.colorCode = p.color;
            placed.x = p.t[0];
            placed.y = p.t[1];
            placed.z = p.t[2];
            std::copy(std::begin(p.m), std::end(p.m), std::begin(placed.m));
            out_.parts.push_back(std::move(placed));
        }
        for (const LDrawPrimitive& prim : block.primitives) {
            if (++items_ > kMaxFlattenedItems) return false;
            LDrawPrimitive placed = prim;
            placed.colorCode = at.colorOf(prim.colorCode);
            for (int i = 0; i < prim.kind; ++i) at.apply(prim.v[i], placed.v[i]);
            out_.primitives.push_back(placed);
        }
        return true;
    }

private:
    const std::map<QString, LDrawBlock>& blocks_;
    LDrawReadResult& out_;
    std::size_t items_ = 0;
};

}  // namespace

LDrawReadResult readLDraw(const QString& path) {
    LDrawReadResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.error = f.errorString();
        return r;
    }
    QTextStream in(&f);

    // Multi-part files (.mpd, Studio's model.ldr) hold "0 FILE <name>"
    // blocks: the first is the model, the others are submodels it places.
    // std::map: pointers to its blocks stay valid as more are added.
    std::map<QString, LDrawBlock> blocks;
    LDrawBlock single, ignored;
    LDrawBlock* current = &single;
    LDrawBlock* main = nullptr;
    QString mainKey;
    bool firstComment = true;
    QString line;
    static const QRegularExpression kWs(QStringLiteral("\\s+"));
    while (in.readLineInto(&line)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        // Split on any whitespace. Pre-compiled regex; rebuilding per
        // line dominated parse time on big .ldr models.
        const auto parts = trimmed.split(kWs, Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        bool ok = false;
        const int code = parts[0].toInt(&ok);
        if (!ok) continue;

        if (code == 0 && parts.size() > 2 && parts[1] == QLatin1String("FILE")) {
            const QString name = trimmed.mid(trimmed.indexOf(QLatin1String("FILE")) + 4).trimmed();
            if (firstComment) {
                // "0 FILE Ninjago City.io" names the model: drop the extension.
                QString title = name;
                for (const char* ext : { ".io", ".ldr", ".mpd", ".dat" })
                    if (title.endsWith(QLatin1String(ext), Qt::CaseInsensitive)) title.chop(int(qstrlen(ext)));
                r.title = title;
                firstComment = false;
            }
            const QString key = name.toLower();
            if (blocks.count(key)) {
                current = &ignored;  // a repeated name: the first one counts
                continue;
            }
            current = &blocks[key];
            if (!main) {
                main = current;
                mainKey = key;
            }
            continue;
        }
        if (code == 0 && firstComment) {
            // Line-0 comments start with "0" followed by text.
            if (parts.size() > 1) {
                r.title = trimmed.mid(1).trimmed();
            }
            firstComment = false;
            continue;
        }
        if (code == 1) {
            // Expect: 1 color x y z a b c d e f g h i filename
            if (parts.size() < 15) continue;

            LDrawPartRef p;
            p.colorCode = parts[1].toInt();
            p.x = parts[2].toDouble();
            p.y = parts[3].toDouble();
            p.z = parts[4].toDouble();
            for (int i = 0; i < 9; ++i) {
                p.m[i] = parts[5 + i].toDouble();
            }
            // Filename is the rest of the line (can contain spaces).
            p.filename = parts.mid(14).join(QLatin1Char(' '));
            current->parts.push_back(std::move(p));
            continue;
        }
        // Primitive: 2 = line (2 verts), 3 = tri (3 verts), 4 = quad (4 verts).
        // Type 5 is conditional-line, skipped entirely — it's a rendering
        // hint for edge detection, not geometry we want to rasterize.
        if (code == 2 || code == 3 || code == 4) {
            const int nVerts = code;
            // Expect: <code> colour  <3 * nVerts floats>
            if (parts.size() < 2 + 3 * nVerts) continue;
            LDrawPrimitive p;
            p.kind = code;
            p.colorCode = parts[1].toInt();
            for (int i = 0; i < nVerts; ++i) {
                p.v[i][0] = parts[2 + i * 3 + 0].toDouble();
                p.v[i][1] = parts[2 + i * 3 + 1].toDouble();
                p.v[i][2] = parts[2 + i * 3 + 2].toDouble();
            }
            current->primitives.push_back(p);
        }
    }

    if (!main) {
        r.parts = std::move(single.parts);
        r.primitives = std::move(single.primitives);
        r.ok = true;
        return r;
    }
    // Lines before the first "0 FILE" belong to the model too.
    QStringList stack;
    Flattener flat(blocks, r);
    if (!flat.add(single, Placement(), 0, stack)) {
        r.error = QStringLiteral("The model is too large or its submodels nest too deeply.");
        return r;
    }
    stack << mainKey;
    if (!flat.add(*main, Placement(), 0, stack)) {
        r.parts.clear();
        r.primitives.clear();
        r.error = QStringLiteral("The model is too large or its submodels nest too deeply.");
        return r;
    }
    r.ok = true;
    return r;
}

namespace {

// Library key for an LDraw reference: exact "<part>.<colour>", then the
// part in any colour, then the same two for each earlier LDraw number
// of a renumbered part. Empty when nothing in `lib` matches.
QString resolvePartKey(const LDrawPartRef& ref, const QString& partNumber, int colorCode,
                       const parts::PartsLibrary& lib, const LDrawLibrary* ldraw) {
    QStringList names{ partNumber };
    for (const QString& alias : ref.aliases) {
        const QString pn = partNumberFromFilename(alias);
        if (!names.contains(pn)) names << pn;
    }
    if (ldraw) {
        for (const QString& n : QStringList(names)) {
            for (const QString& old : ldraw->formerNames(n)) {
                if (!names.contains(old.toUpper())) names << old.toUpper();
            }
        }
    }
    // LDraw suffixes pre-assembled parts with cNN ("2861c01" = the 9V
    // switch with its lever); BlueBrickParts lists the base number.
    static const QRegularExpression assembly(QStringLiteral("^(.*\\d)C\\d\\d$"),
                                             QRegularExpression::CaseInsensitiveOption);
    QStringList candidates;
    for (const QString& n : names) {
        candidates << n;
        const auto m = assembly.match(n);
        if (m.hasMatch()) candidates << m.captured(1).toUpper();
    }
    const QStringList keys = lib.keys();
    for (const QString& pn : candidates) {
        const QString exact = QStringLiteral("%1.%2").arg(pn).arg(colorCode);
        if (lib.metadata(exact)) return exact;
        if (lib.metadata(pn)) return pn;
        const QString prefix = pn.toLower() + QLatin1Char('.');
        for (const QString& k : keys) {
            if (k.startsWith(prefix)) return k;
        }
    }
    return {};
}

struct GeometryCentre {
    QPointF centre;  // top-down, BlueBrick frame (y = -z), LDU
    double  size;    // larger side of the part's footprint, LDU
};

// Centre of the part's own top-down bounding box, placed by `ref`.
std::optional<GeometryCentre> geometryCentre(const LDrawPartRef& ref, LDrawMeshLoader& loader) {
    const geom::Mesh mesh = loader.loadPart(ref.filename, ref.colorCode);
    if (mesh.tris.empty()) return std::nullopt;
    double xmin = 1e300, xmax = -1e300, zmin = 1e300, zmax = -1e300;
    for (const auto& t : mesh.tris) {
        for (const auto& v : t.v) {
            xmin = std::min(xmin, v.x); xmax = std::max(xmax, v.x);
            zmin = std::min(zmin, v.z); zmax = std::max(zmax, v.z);
        }
    }
    const double cx = (xmin + xmax) / 2.0, cz = (zmin + zmax) / 2.0;
    // Local (x, 0, z) through the ref's rotation + translation.
    const double wx = ref.m[0] * cx + ref.m[2] * cz + ref.x;
    const double wz = ref.m[6] * cx + ref.m[8] * cz + ref.z;
    return GeometryCentre{ QPointF(wx, -wz), std::max(xmax - xmin, zmax - zmin) };
}

}  // namespace

std::unique_ptr<core::Map> toBlueBrickMap(const LDrawReadResult& src,
                                          const parts::PartsLibrary* lib,
                                          const LDrawLibrary* ldraw,
                                          LDrawMeshLoader* geometry) {
    auto map = std::make_unique<core::Map>();
    map->author = QStringLiteral("LDraw import");
    if (!src.title.isEmpty()) map->comment = src.title;

    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    layer->name = QStringLiteral("Imported");

    for (const auto& ref : src.parts) {
        core::Brick b;
        b.guid = core::newBbmId();
        const QString pn = partNumberFromFilename(ref.filename);
        // BlueBrick format bakes color into PartNumber as "<part>.<color>".
        b.partNumber = QStringLiteral("%1.%2").arg(pn).arg(ref.colorCode);

        // Same mapping as BlueBrick's LDraw loader (parseBrickLineLDRAW):
        // (LDD reads keep their long-standing y = +z and skip the LDraw
        // remap, which is keyed on LDraw geometry.)
        // top-down with y = -z (LDraw is -Y up), angle = atan2(c, a), then
        // the part's <LDraw> remap rotates/shifts the LDraw origin onto
        // the image centre.
        double angle = yRotationDegrees(ref.m);
        double x = ref.x;
        double y = src.lddAxes ? ref.z : -ref.z;
        if (lib && !src.lddAxes) {
            const QString key = resolvePartKey(ref, pn, ref.colorCode, *lib, ldraw);
            if (!key.isEmpty()) {
                const auto meta = lib->metadata(key);
                b.partNumber = meta->colorCode.isEmpty()
                    ? meta->partNumber
                    : QStringLiteral("%1.%2").arg(meta->partNumber, meta->colorCode);
                angle -= meta->ldrawAngle;
                const QPointF t = meta->ldrawTranslation;
                if (!t.isNull()) {
                    const double r = qDegreesToRadians(angle);
                    const double c = std::cos(r), s = std::sin(r);
                    // Rotate (-tx, ty) by `angle` (y-down screen rotation).
                    x += -t.x() * c - t.y() * s;
                    y += -t.x() * s + t.y() * c;
                }
                if (geometry) {
                    if (const auto g = geometryCentre(ref, *geometry)) {
                        const double dist = std::hypot(g->centre.x() - x, g->centre.y() - y);
                        if (dist > 0.25 * g->size) { x = g->centre.x(); y = g->centre.y(); }
                    }
                }
            }
        }
        b.orientation = static_cast<float>(angle);
        b.altitude = static_cast<float>(ref.y / kLduPerStud);

        const double xStuds = x / kLduPerStud;
        const double yStuds = y / kLduPerStud;
        // (x, y) is the sprite centre. Without a library, parts get a
        // 2x2 placeholder centred on it.
        if (lib) {
            // Only fills the library's sprite cache.
            parts::placement::placeByImageCentre(b, QPointF(xStuds, yStuds),
                                                 const_cast<parts::PartsLibrary&>(*lib));
        } else {
            constexpr double defaultStuds = 2.0;
            b.displayArea = QRectF(xStuds - defaultStuds / 2.0,
                                    yStuds - defaultStuds / 2.0,
                                    defaultStuds, defaultStuds);
        }
        layer->bricks.push_back(std::move(b));
    }

    map->layers().push_back(std::move(layer));
    return map;
}

}
