#include "SidecarIO.h"

#include "../core/Sidecar.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

namespace bld::saveload {

namespace {

QJsonObject encodeColor(const core::ColorSpec& c) {
    QJsonObject o;
    o[QStringLiteral("known")] = c.isKnown();
    o[QStringLiteral("argb")]  = static_cast<qint64>(c.color.rgba());
    if (c.isKnown()) o[QStringLiteral("name")] = c.knownName;
    return o;
}

core::ColorSpec decodeColor(const QJsonObject& o) {
    core::ColorSpec c;
    c.color = QColor::fromRgba(static_cast<quint32>(o.value(QStringLiteral("argb")).toInteger()));
    if (o.value(QStringLiteral("known")).toBool()) c.knownName = o.value(QStringLiteral("name")).toString();
    return c;
}

QJsonObject encodeFont(const core::FontSpec& f) {
    QJsonObject o;
    o[QStringLiteral("family")] = f.familyName;
    o[QStringLiteral("size")]   = f.sizePt;
    o[QStringLiteral("style")]  = f.styleString;
    return o;
}

core::FontSpec decodeFont(const QJsonObject& o) {
    core::FontSpec f;
    f.familyName  = o.value(QStringLiteral("family")).toString(f.familyName);
    f.sizePt      = static_cast<float>(o.value(QStringLiteral("size")).toDouble(f.sizePt));
    f.styleString = o.value(QStringLiteral("style")).toString(f.styleString);
    return f;
}

QJsonObject encodePoint(QPointF p) {
    QJsonObject o;
    o[QStringLiteral("x")] = p.x();
    o[QStringLiteral("y")] = p.y();
    return o;
}

QPointF decodePoint(const QJsonObject& o) {
    return { o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble() };
}

QJsonObject encodeAnchored(const core::AnchoredLabel& a) {
    QJsonObject o;
    o[QStringLiteral("id")] = a.id;
    o[QStringLiteral("text")] = a.text;
    o[QStringLiteral("font")] = encodeFont(a.font);
    o[QStringLiteral("color")] = encodeColor(a.color);
    o[QStringLiteral("kind")] = static_cast<int>(a.kind);
    o[QStringLiteral("targetId")] = a.targetId;
    o[QStringLiteral("offset")] = encodePoint(a.offset);
    o[QStringLiteral("rot")] = a.offsetRotation;
    o[QStringLiteral("minZoom")] = a.minZoom;
    return o;
}

core::AnchoredLabel decodeAnchored(const QJsonObject& o) {
    core::AnchoredLabel a;
    a.id = o.value(QStringLiteral("id")).toString();
    a.text = o.value(QStringLiteral("text")).toString();
    a.font = decodeFont(o.value(QStringLiteral("font")).toObject());
    a.color = decodeColor(o.value(QStringLiteral("color")).toObject());
    a.kind = static_cast<core::AnchorKind>(o.value(QStringLiteral("kind")).toInt());
    a.targetId = o.value(QStringLiteral("targetId")).toString();
    a.offset = decodePoint(o.value(QStringLiteral("offset")).toObject());
    a.offsetRotation = static_cast<float>(o.value(QStringLiteral("rot")).toDouble());
    a.minZoom = o.value(QStringLiteral("minZoom")).toDouble();
    return a;
}

QJsonObject encodeModule(const core::Module& m) {
    QJsonObject o;
    o[QStringLiteral("id")] = m.id;
    o[QStringLiteral("name")] = m.name;
    QJsonArray members;
    for (const auto& id : m.memberIds) members.append(id);
    o[QStringLiteral("members")] = members;
    QJsonArray xform{ m.transform.m11(), m.transform.m12(), m.transform.m13(),
                      m.transform.m21(), m.transform.m22(), m.transform.m23(),
                      m.transform.m31(), m.transform.m32(), m.transform.m33() };
    o[QStringLiteral("transform")] = xform;
    if (!m.sourceFile.isEmpty()) o[QStringLiteral("sourceFile")] = m.sourceFile;
    if (m.importedAt.isValid()) {
        o[QStringLiteral("importedAt")] = m.importedAt.toString(Qt::ISODate);
    }
    return o;
}

core::Module decodeModule(const QJsonObject& o) {
    core::Module m;
    m.id = o.value(QStringLiteral("id")).toString();
    m.name = o.value(QStringLiteral("name")).toString();
    for (const auto& v : o.value(QStringLiteral("members")).toArray()) {
        m.memberIds.insert(v.toString());
    }
    auto x = o.value(QStringLiteral("transform")).toArray();
    if (x.size() >= 9) {
        m.transform = QTransform(
            x[0].toDouble(), x[1].toDouble(), x[2].toDouble(),
            x[3].toDouble(), x[4].toDouble(), x[5].toDouble(),
            x[6].toDouble(), x[7].toDouble(), x[8].toDouble());
    }
    m.sourceFile = o.value(QStringLiteral("sourceFile")).toString();
    const QString at = o.value(QStringLiteral("importedAt")).toString();
    if (!at.isEmpty()) m.importedAt = QDateTime::fromString(at, Qt::ISODate);
    return m;
}

QJsonObject encodeVenue(const core::Venue& v) {
    QJsonObject o;
    o[QStringLiteral("name")] = v.name;
    o[QStringLiteral("enabled")] = v.enabled;
    o[QStringLiteral("minWalkwayStuds")] = v.minWalkwayStuds;
    QJsonObject bounds;
    bounds[QStringLiteral("x")] = v.layoutBoundsStuds.x();
    bounds[QStringLiteral("y")] = v.layoutBoundsStuds.y();
    bounds[QStringLiteral("w")] = v.layoutBoundsStuds.width();
    bounds[QStringLiteral("h")] = v.layoutBoundsStuds.height();
    o[QStringLiteral("bounds")] = bounds;

    QJsonArray edges;
    for (const auto& e : v.edges) {
        QJsonObject eo;
        eo[QStringLiteral("kind")] = static_cast<int>(e.kind);
        eo[QStringLiteral("doorWidthStuds")] = e.doorWidthStuds;
        eo[QStringLiteral("label")] = e.label;
        QJsonArray pts;
        for (const auto& p : e.polyline) pts.append(encodePoint(p));
        eo[QStringLiteral("poly")] = pts;
        edges.append(eo);
    }
    o[QStringLiteral("edges")] = edges;

    QJsonArray obstacles;
    for (const auto& ob : v.obstacles) {
        QJsonObject oo;
        oo[QStringLiteral("label")] = ob.label;
        QJsonArray pts;
        for (const auto& p : ob.polygon) pts.append(encodePoint(p));
        oo[QStringLiteral("poly")] = pts;
        obstacles.append(oo);
    }
    o[QStringLiteral("obstacles")] = obstacles;

    return o;
}

core::Venue decodeVenue(const QJsonObject& o) {
    core::Venue v;
    v.name = o.value(QStringLiteral("name")).toString();
    v.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    v.minWalkwayStuds = o.value(QStringLiteral("minWalkwayStuds")).toDouble(v.minWalkwayStuds);
    auto bounds = o.value(QStringLiteral("bounds")).toObject();
    v.layoutBoundsStuds = QRectF(
        bounds.value(QStringLiteral("x")).toDouble(),
        bounds.value(QStringLiteral("y")).toDouble(),
        bounds.value(QStringLiteral("w")).toDouble(),
        bounds.value(QStringLiteral("h")).toDouble());
    for (const auto& v2 : o.value(QStringLiteral("edges")).toArray()) {
        const auto eo = v2.toObject();
        core::VenueEdge e;
        e.kind = static_cast<core::EdgeKind>(eo.value(QStringLiteral("kind")).toInt());
        e.doorWidthStuds = eo.value(QStringLiteral("doorWidthStuds")).toDouble();
        e.label = eo.value(QStringLiteral("label")).toString();
        for (const auto& p : eo.value(QStringLiteral("poly")).toArray()) {
            e.polyline.append(decodePoint(p.toObject()));
        }
        v.edges.append(e);
    }
    for (const auto& v2 : o.value(QStringLiteral("obstacles")).toArray()) {
        const auto oo = v2.toObject();
        core::VenueObstacle ob;
        ob.label = oo.value(QStringLiteral("label")).toString();
        for (const auto& p : oo.value(QStringLiteral("poly")).toArray()) {
            ob.polygon.append(decodePoint(p.toObject()));
        }
        v.obstacles.append(ob);
    }
    return v;
}

}

QString sidecarPathFor(const QString& bbmPath) {
    return bbmPath + QStringLiteral(".bld");
}

QByteArray sha256Hex(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

SidecarLoadResult readSidecar(const QString& cldPath, const QByteArray& bbmBytes,
                               core::Sidecar& out) {
    SidecarLoadResult r;
    QFile f(cldPath);
    if (!f.open(QIODevice::ReadOnly)) { r.error = f.errorString(); return r; }
    const QByteArray bytes = f.readAll();

    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = pe.errorString();
        return r;
    }
    sidecarFromJson(doc.object(), out);

    if (!bbmBytes.isEmpty() && !out.bbmContentHashSha256.isEmpty()) {
        r.hashMismatch = (sha256Hex(bbmBytes) != out.bbmContentHashSha256);
    }
    r.ok = true;
    return r;
}

QJsonObject sidecarToJson(const core::Sidecar& sidecar) {
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = core::Sidecar::kSchemaVersion;
    root[QStringLiteral("bbmHashSha256")] = QString::fromUtf8(sidecar.bbmContentHashSha256);

    QJsonArray labels;
    for (const auto& a : sidecar.anchoredLabels) labels.append(encodeAnchored(a));
    root[QStringLiteral("anchoredLabels")] = labels;

    QJsonArray modules;
    for (const auto& m : sidecar.modules) modules.append(encodeModule(m));
    root[QStringLiteral("modules")] = modules;

    if (sidecar.venue) { root[QStringLiteral("venue")] = encodeVenue(*sidecar.venue); }

    if (!sidecar.backgroundImagePath.isEmpty()) {
        QJsonObject bg;
        bg[QStringLiteral("path")] = sidecar.backgroundImagePath;
        bg[QStringLiteral("opacity")] = sidecar.backgroundImageOpacity;
        if (!sidecar.backgroundImageRectStuds.isNull()) {
            const auto& r = sidecar.backgroundImageRectStuds;
            bg[QStringLiteral("rect")] = QJsonArray{ r.x(), r.y(), r.width(), r.height() };
        }
        root[QStringLiteral("backgroundImage")] = bg;
    }
    return root;
}

void sidecarFromJson(const QJsonObject& root, core::Sidecar& out) {
    out.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(core::Sidecar::kSchemaVersion);
    out.bbmContentHashSha256 = root.value(QStringLiteral("bbmHashSha256")).toString().toUtf8();

    out.anchoredLabels.clear();
    for (const auto& v : root.value(QStringLiteral("anchoredLabels")).toArray()) {
        out.anchoredLabels.push_back(decodeAnchored(v.toObject()));
    }
    out.modules.clear();
    for (const auto& v : root.value(QStringLiteral("modules")).toArray()) {
        out.modules.push_back(decodeModule(v.toObject()));
    }
    out.venue.reset();
    if (root.contains(QStringLiteral("venue"))) {
        out.venue = decodeVenue(root.value(QStringLiteral("venue")).toObject());
    }

    out.backgroundImagePath.clear();
    out.backgroundImageRectStuds = QRectF();
    out.backgroundImageOpacity = 0.5;
    if (root.contains(QStringLiteral("backgroundImage"))) {
        const auto bg = root.value(QStringLiteral("backgroundImage")).toObject();
        out.backgroundImagePath = bg.value(QStringLiteral("path")).toString();
        out.backgroundImageOpacity = bg.value(QStringLiteral("opacity")).toDouble(0.5);
        if (bg.contains(QStringLiteral("rect"))) {
            const auto r = bg.value(QStringLiteral("rect")).toArray();
            if (r.size() == 4) {
                out.backgroundImageRectStuds = QRectF(
                    r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
            }
        }
    }
}

bool writeSidecar(const QString& cldPath, const QByteArray& bbmBytes,
                  const core::Sidecar& sidecar, QString* error) {
    QJsonObject root = sidecarToJson(sidecar);
    root[QStringLiteral("bbmHashSha256")] = QString::fromUtf8(sha256Hex(bbmBytes));

    QSaveFile f(cldPath);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

}
