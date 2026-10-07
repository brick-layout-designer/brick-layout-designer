#include "LDDLDrawMapping.h"

#include <array>
#include <cmath>

#include <QFile>
#include <QXmlStreamReader>

namespace bld::import {

namespace {

// Train track, from the community ldraw.xml (slswww.free.fr/ldraw.xml):
// LDD's bundled ldraw.xml predates it, and without these LDD track
// can't be matched to BlueBrick's track parts, so it gets no snap
// points. Used only where the loaded file has no entry of its own.
struct BuiltinTrack {
    const char* lego;   // LDD design ID
    const char* ldraw;  // LDraw file
    double tx, ty, tz, angle;  // about +Y
};
constexpr BuiltinTrack kBuiltinTrack[] = {
    { "53401", "53401.dat", 6, -0.32, 2.8, 1.570796 },          // RC straight
    { "53400", "53400.dat", -6.4, -0.32, -2.2, -1.374447 },      // RC curve
    { "74746", "74746.dat", 6, -0.32, 2.8, 1.570796 },          // 9V straight
    { "2865", "2865.dat", 6, -0.32, 2.8, 1.570796 },
    { "74747", "74747.dat", -6.4, -0.32, -2.24, -1.374447 },     // 9V curve
    { "2867", "2867.dat", -6.4, -0.32, -2.24, -1.374447 },
    { "75541", "75541.dat", -12.4, -0.32, -2.8, -1.570796 },     // 9V point right
    { "75542", "75542.dat", -12.4, -0.32, -2.8, -1.570796 },     // 9V point left
    { "64573", "88492.dat", 0, -0.32, 0, 1.570796 },            // flex, female half
    { "64572", "88493.dat", 0, -0.32, 0, 1.570796 },            // flex, male half
    { "85976", "85976.dat", -7.74, -0.32, -0.54, -1.178097 },    // 4-wide curve
    { "85977", "85977.dat", -6, -5.12, -2, -1.570796 },          // 4-wide ramp
};

const BuiltinTrack* builtinByLego(const QString& id) {
    for (const auto& t : kBuiltinTrack)
        if (id == QLatin1String(t.lego)) return &t;
    return nullptr;
}

const BuiltinTrack* builtinByLdraw(const QString& dat) {
    for (const auto& t : kBuiltinTrack)
        if (dat.compare(QLatin1String(t.ldraw), Qt::CaseInsensitive) == 0) return &t;
    return nullptr;
}

}  // namespace

QString LDDLDrawMapping::partFor(const QString& lddDesignId) const {
    QString mapped = brickToLdraw_.value(lddDesignId);
    if (!mapped.isEmpty()) return mapped;
    const BuiltinTrack* t = builtinByLego(lddDesignId);
    return t ? QString::fromLatin1(t->ldraw) : QString();
}

LDDLDrawMapping::Transformation LDDLDrawMapping::transformFor(const QString& ldrawDat) const {
    if (const auto it = transformations_.constFind(ldrawDat); it != transformations_.constEnd()) return it.value();
    Transformation out;
    if (const BuiltinTrack* t = builtinByLdraw(ldrawDat)) {
        out.exists = true;
        out.tx = t->tx;
        out.ty = t->ty;
        out.tz = t->tz;
        out.ax = 0;
        out.ay = 1;
        out.az = 0;
        out.angle = t->angle;
    }
    return out;
}

bool LDDLDrawMapping::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QXmlStreamReader r(&f);
    int added = 0;

    // ldraw.xml is a flat list of mapping elements; no nesting. Scan
    // every start element and dispatch on tag name. The <LDrawMapping>
    // root just holds the version and is otherwise empty.
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        const auto attrs = r.attributes();
        const QStringView name = r.name();
        if (name == QStringLiteral("Material")) {
            bool ok1 = false, ok2 = false;
            const int ldraw = attrs.value(QStringLiteral("ldraw")).toInt(&ok1);
            const int lego  = attrs.value(QStringLiteral("lego")).toInt(&ok2);
            if (ok1 && ok2) {
                materialToLdraw_.insert(lego, ldraw);
                ++added;
            }
        } else if (name == QStringLiteral("Brick") ||
                   name == QStringLiteral("Assembly")) {
            // <Brick ldraw="X.dat" lego="Y" />            — single-part
            // <Assembly ldraw="X.dat" lego="Y" tx ty tz>  — multi-part;
            //     children are <Part ldraw="...">, but the assembly's
            //     own ldraw= still names the canonical LDraw .dat the
            //     assembly maps to. Treat assemblies the same as
            //     bricks for top-level lookups.
            const QString ldraw = attrs.value(QStringLiteral("ldraw")).toString().trimmed();
            const QString lego  = attrs.value(QStringLiteral("lego")).toString().trimmed();
            if (!ldraw.isEmpty() && !lego.isEmpty()) {
                brickToLdraw_.insert(lego, ldraw);
                QStringList& all = brickAliases_[lego];
                if (!all.contains(ldraw, Qt::CaseInsensitive)) all.append(ldraw);
                ++added;
            }
        } else if (name == QStringLiteral("Transformation")) {
            const QString ldraw = attrs.value(QStringLiteral("ldraw")).toString().trimmed();
            if (ldraw.isEmpty()) continue;
            Transformation t;
            t.exists = true;
            t.tx = attrs.value(QStringLiteral("tx")).toDouble();
            t.ty = attrs.value(QStringLiteral("ty")).toDouble();
            t.tz = attrs.value(QStringLiteral("tz")).toDouble();
            t.ax = attrs.value(QStringLiteral("ax")).toDouble();
            t.ay = attrs.value(QStringLiteral("ay")).toDouble();
            t.az = attrs.value(QStringLiteral("az")).toDouble();
            t.angle = attrs.value(QStringLiteral("angle")).toDouble();
            transformations_.insert(ldraw, t);
            ++added;
        }
    }
    return added > 0 && !r.hasError();
}

namespace {

using Mat3 = std::array<double, 9>;  // row-major, column-vector convention

Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
    return r;
}

// Right-handed rotation about (x, y, z) by `radians` (Rodrigues).
Mat3 axisAngle(double x, double y, double z, double radians) {
    const double len = std::sqrt(x * x + y * y + z * z);
    if (len == 0.0 || radians == 0.0) return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    x /= len; y /= len; z /= len;
    const double c = std::cos(radians), s = std::sin(radians), ic = 1.0 - c;
    Mat3 m{ x * x * ic + c,     x * y * ic - z * s, x * z * ic + y * s,
            y * x * ic + z * s, y * y * ic + c,     y * z * ic - x * s,
            x * z * ic - y * s, y * z * ic + x * s, z * z * ic + c };
    // ldraw.xml angles are radians rounded to 6 places; snap the noise so
    // quarter turns come out as exact 0 / ±1 like LDraw files expect.
    for (double& v : m) {
        if (std::abs(v) < 1e-6) v = 0.0;
        else if (std::abs(std::abs(v) - 1.0) < 1e-6) v = std::copysign(1.0, v);
    }
    return m;
}

}  // namespace

LDrawReadResult LDDLDrawMapping::toLDraw(const LDrawReadResult& ldd) const {
    // F: 180° about X — LDD is +Y up, LDraw is -Y up. 25 LDU per LDD unit
    // is already applied by readLDD.
    constexpr Mat3 F{ 1, 0, 0, 0, -1, 0, 0, 0, -1 };
    constexpr double kLduPerLddUnit = 25.0;

    LDrawReadResult out;
    out.ok = ldd.ok;
    out.error = ldd.error;
    out.title = ldd.title;
    out.lddAxes = false;
    for (const LDrawPartRef& ref : ldd.parts) {
        // readLDD names parts "<designID>.<material>.dat".
        const QString designId = ref.filename.section(QLatin1Char('.'), 0, 0);
        QString dat = partFor(designId);
        if (dat.isEmpty()) dat = designId + QStringLiteral(".dat");
        const int colour = colourFor(ref.colorCode);

        const Mat3 L{ ref.m[0], ref.m[1], ref.m[2], ref.m[3], ref.m[4],
                      ref.m[5], ref.m[6], ref.m[7], ref.m[8] };
        Mat3 rot = L;
        double px = ref.x, py = ref.y, pz = ref.z;
        const Transformation t = transformFor(dat);
        if (t.exists) {
            // lxf2ldr: rotation about the axis by -angle; translation
            // -(tx, ty, tz) in the part's rotated LDD frame.
            rot = mul(L, axisAngle(t.ax, t.ay, t.az, -t.angle));
            const double mx = -t.tx, my = -t.ty, mz = -t.tz;
            px += kLduPerLddUnit * (rot[0] * mx + rot[1] * my + rot[2] * mz);
            py += kLduPerLddUnit * (rot[3] * mx + rot[4] * my + rot[5] * mz);
            pz += kLduPerLddUnit * (rot[6] * mx + rot[7] * my + rot[8] * mz);
        }
        const Mat3 m = mul(mul(F, rot), F);

        LDrawPartRef o;
        o.colorCode = colour >= 0 ? colour : ref.colorCode;
        o.x = px;
        o.y = -py;
        o.z = -pz;
        for (int i = 0; i < 9; ++i) o.m[i] = m[i];
        o.filename = dat;
        for (const QString& alias : partsFor(designId)) {
            if (alias.compare(dat, Qt::CaseInsensitive) != 0) o.aliases << alias;
        }
        out.parts.push_back(std::move(o));
    }
    return out;
}

}  // namespace bld::import
