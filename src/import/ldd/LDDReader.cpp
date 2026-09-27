#include "LDDReader.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QXmlStreamReader>

#include "../zip/SafeZip.h"

namespace bld::import {

namespace {

// Pull the LXFML document out of an .lxf ZIP. LDD stores it at the
// archive root with the filename `LXFML` (no extension). Falls back to
// any `.lxfml` file on mismatch.
QByteArray extractLxfmlFromLxf(const QString& path, QString* err) {
    const auto zip = SafeZip::open(path);
    if (!zip) {
        if (err) *err = QStringLiteral("Not a readable LDD .lxf archive: %1").arg(path);
        return {};
    }
    const SafeZip::Entry* entry = zip->find(QStringLiteral("LXFML"), Qt::CaseInsensitive);
    if (!entry) {
        for (const auto& e : zip->entries())
            if (!e.isDir && e.name.endsWith(QStringLiteral(".lxfml"), Qt::CaseInsensitive)) { entry = &e; break; }
    }
    if (!entry) {
        if (err) *err = QStringLiteral("No LXFML entry found inside %1").arg(path);
        return {};
    }
    constexpr qint64 kMaxModelBytes = qint64(64) << 20;
    auto data = zip->read(*entry, kMaxModelBytes);
    if (!data) {
        if (err) *err = QStringLiteral("The LXFML inside %1 is damaged").arg(path);
        return {};
    }
    return *data;
}

// Parse an LDD transformation string. The 12 values serialize as
// "a,b,c,d,e,f,g,h,i,tx,ty,tz" — despite looking like row-major
// "m00,m01,m02,...", LDD actually stores the rotation **column-major**:
// (a,d,g) is the first column, (b,e,h) the second, etc. (Confirmed
// against lu-toolbox importldd.py, which multiplies via n11/n21/n31
// — the first column.) We transpose into row-major here so every
// downstream consumer can treat LDrawPartRef::m the same as LDraw's.
// Returns a sentinel (ok=false) on parse failure.
struct LddTransform {
    double m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    double tx = 0, ty = 0, tz = 0;
    bool ok = false;
};
LddTransform parseTransform(QStringView s) {
    LddTransform t;
    const auto parts = s.toString().split(QLatin1Char(','));
    if (parts.size() < 12) return t;
    double col[9];
    for (int i = 0; i < 9; ++i) col[i] = parts[i].toDouble();
    // col is in (col0, col1, col2) layout; transpose to row-major.
    t.m[0] = col[0]; t.m[1] = col[3]; t.m[2] = col[6];
    t.m[3] = col[1]; t.m[4] = col[4]; t.m[5] = col[7];
    t.m[6] = col[2]; t.m[7] = col[5]; t.m[8] = col[8];
    t.tx = parts[9].toDouble();
    t.ty = parts[10].toDouble();
    t.tz = parts[11].toDouble();
    t.ok = true;
    return t;
}

}  // namespace

LDrawReadResult readLDD(const QString& path) {
    LDrawReadResult out;
    out.lddAxes = true;

    // Decide whether the input is a .lxf ZIP or raw .lxfml XML.
    QByteArray xmlBytes;
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".lxf"))) {
        xmlBytes = extractLxfmlFromLxf(path, &out.error);
        if (xmlBytes.isEmpty()) return out;
    } else if (lower.endsWith(QStringLiteral(".lxfml"))) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            out.error = QStringLiteral("Cannot open %1: %2").arg(path, f.errorString());
            return out;
        }
        xmlBytes = f.readAll();
    } else {
        out.error = QStringLiteral("Not a .lxf or .lxfml file: %1").arg(path);
        return out;
    }

    QXmlStreamReader r(xmlBytes);
    // LXFML coordinate system: 1 LDD unit = 1.25 studs (= 0.4 mm
    // unit, with 1 stud = 8 mm; 8 / 6.4 = 1.25). LDU is 1 stud / 20,
    // so 1 LDD unit = 25 LDU. Verified empirically: at 20 the rendered
    // sprite measured 16 studs wide for a brick known to be 20 studs
    // (16/20 = 0.8 = 20/25, exactly the missing factor).
    constexpr double kLddToLdu = 25.0;

    while (!r.atEnd() && !r.hasError()) {
        const auto token = r.readNext();
        if (token != QXmlStreamReader::StartElement) continue;
        const auto name = r.name();
        if (name == QStringLiteral("LXFML")) {
            out.title = r.attributes().value(QStringLiteral("name")).toString();
            if (out.title.isEmpty()) out.title = QFileInfo(path).completeBaseName();
            continue;
        }
        if (name == QStringLiteral("Brick")) {
            const QString designID = r.attributes().value(QStringLiteral("designID")).toString();
            int matId = 0;
            // Walk Part → Bone to find transform + material. We take the
            // FIRST transform we encounter — sufficient for top-down
            // sprites since LDD rarely multi-bones a simple brick.
            LddTransform xform;
            while (!r.atEnd()) {
                const auto inner = r.readNext();
                if (inner == QXmlStreamReader::EndElement
                    && r.name() == QStringLiteral("Brick")) break;
                if (inner != QXmlStreamReader::StartElement) continue;
                if (r.name() == QStringLiteral("Part") && matId == 0) {
                    matId = r.attributes().value(QStringLiteral("materials")).toString()
                            .split(QLatin1Char(',')).value(0).toInt();
                }
                if (r.name() == QStringLiteral("Bone") && !xform.ok) {
                    xform = parseTransform(r.attributes().value(QStringLiteral("transformation")));
                }
            }
            if (!xform.ok) continue;
            LDrawPartRef ref;
            ref.colorCode = matId > 0 ? matId : 1;  // default to light-gray
            // LDD ↔ LDraw 1:1 unit mapping; we kept LDD's native axes
            // (Y up, Z toward viewer). MeshRasterize projects (X,Z)
            // for the top-down sprite.
            ref.x = xform.tx * kLddToLdu;
            ref.y = xform.ty * kLddToLdu;
            ref.z = xform.tz * kLddToLdu;
            for (int i = 0; i < 9; ++i) ref.m[i] = xform.m[i];
            // Part filename: try "<designID>.<matId>.dat" so the
            // existing LDraw → BlueBrick mapping strips the .dat and
            // leaves a `<designID>.<matId>` key that matches our
            // library's naming when available.
            if (matId > 0) {
                ref.filename = QStringLiteral("%1.%2.dat").arg(designID).arg(matId);
            } else {
                ref.filename = designID + QStringLiteral(".dat");
            }
            out.parts.push_back(ref);
        }
    }

    out.ok = !r.hasError();
    if (!out.ok) {
        out.error = QStringLiteral("LXFML parse error at line %1 col %2: %3")
            .arg(r.lineNumber()).arg(r.columnNumber()).arg(r.errorString());
    }
    return out;
}

}  // namespace bld::import
