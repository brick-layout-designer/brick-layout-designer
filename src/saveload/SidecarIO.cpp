#include "SidecarIO.h"
#include "VenueJson.h"

#include "../core/Sidecar.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <cmath>

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

// The module fields this build reads; anything else is kept in `extras`.
const QStringList& knownModuleKeys() {
    static const QStringList keys{ QStringLiteral("id"),         QStringLiteral("name"),
                                   QStringLiteral("members"),    QStringLiteral("transform"),
                                   QStringLiteral("sourceFile"), QStringLiteral("importedAt"),
                                   QStringLiteral("showName"),   QStringLiteral("outlineColor"),
                                   QStringLiteral("nameColor"),  QStringLiteral("sameColor") };
    return keys;
}

QJsonObject encodeModule(const core::Module& m) {
    QJsonObject o = m.extras;
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
    // The look: only what differs from the default.
    if (!m.showName) o[QStringLiteral("showName")] = false;
    if (!m.outlineColor.isEmpty()) o[QStringLiteral("outlineColor")] = m.outlineColor;
    if (!m.nameColor.isEmpty()) o[QStringLiteral("nameColor")] = m.nameColor;
    if (!m.sameColor) o[QStringLiteral("sameColor")] = false;
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
    m.showName = o.value(QStringLiteral("showName")).toBool(true);
    m.outlineColor = o.value(QStringLiteral("outlineColor")).toString();
    m.nameColor = o.value(QStringLiteral("nameColor")).toString();
    m.sameColor = o.value(QStringLiteral("sameColor")).toBool(true);
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (!knownModuleKeys().contains(it.key())) m.extras.insert(it.key(), it.value());
    return m;
}

// A view's area: {x, y, w, h}, or [x, y, w, h] as the web also accepts.
std::optional<QRectF> decodeViewRect(const QJsonValue& v) {
    double n[4];
    const auto num = [](const QJsonValue& x, double& out) {
        if (!x.isDouble() || !std::isfinite(x.toDouble())) return false;
        out = x.toDouble();
        return true;
    };
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() != 4) return std::nullopt;
        for (int i = 0; i < 4; ++i)
            if (!num(a[i], n[i])) return std::nullopt;
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        const char* keys[] = { "x", "y", "w", "h" };
        for (int i = 0; i < 4; ++i)
            if (!num(o.value(QLatin1String(keys[i])), n[i])) return std::nullopt;
    } else {
        return std::nullopt;
    }
    return QRectF(n[0], n[1], n[2], n[3]);
}

const QStringList& viewKeys() {
    static const QStringList keys{ QStringLiteral("id"),     QStringLiteral("name"),   QStringLiteral("fit"),
                                   QStringLiteral("rect"),   QStringLiteral("sheets"), QStringLiteral("grid"),
                                   QStringLiteral("labels") };
    return keys;
}
}

QJsonObject viewToJson(const core::SavedView& v) {
    QJsonObject o = v.extras;
    o[QStringLiteral("id")] = v.id;
    o[QStringLiteral("name")] = v.name;
    o[QStringLiteral("fit")] = v.fit;
    if (!v.fit && v.rect) {
        QJsonObject r;
        r[QStringLiteral("x")] = v.rect->x();
        r[QStringLiteral("y")] = v.rect->y();
        r[QStringLiteral("w")] = v.rect->width();
        r[QStringLiteral("h")] = v.rect->height();
        o[QStringLiteral("rect")] = r;
    } else {
        o[QStringLiteral("rect")] = QJsonValue::Null;
    }
    if (v.sheets) o[QStringLiteral("sheets")] = QJsonArray::fromStringList(*v.sheets);
    else o[QStringLiteral("sheets")] = QJsonValue::Null;
    o[QStringLiteral("grid")] = v.grid;
    o[QStringLiteral("labels")] = v.labels;
    return o;
}

std::optional<core::SavedView> viewFromJson(const QJsonValue& raw) {
    if (!raw.isObject()) return std::nullopt;
    const QJsonObject o = raw.toObject();
    const QString id = o.value(QLatin1String("id")).toString();
    if (id.isEmpty()) return std::nullopt;
    core::SavedView v;
    v.id = id;
    v.name = o.value(QLatin1String("name")).toString();
    v.rect = decodeViewRect(o.value(QLatin1String("rect")));
    // A view with no area can only be "fit"; one with an area is fit only when it says so.
    const QJsonValue fit = o.value(QLatin1String("fit"));
    v.fit = fit.isBool() ? (fit.toBool() || !v.rect) : !v.rect;
    if (v.fit) v.rect.reset();
    const QJsonValue sheets = o.value(QLatin1String("sheets"));
    if (sheets.isArray()) {
        QStringList list;
        for (const auto& s : sheets.toArray())
            if (s.isString()) list << s.toString();
        v.sheets = list;
    }
    const QJsonValue grid = o.value(QLatin1String("grid"));
    v.grid = grid.isBool() ? grid.toBool() : true;
    const QJsonValue labels = o.value(QLatin1String("labels"));
    v.labels = labels.isBool() ? labels.toBool() : true;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!viewKeys().contains(it.key())) v.extras.insert(it.key(), it.value());
    return v;
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

    if (sidecar.venue) { root[QStringLiteral("venue")] = venueToJson(*sidecar.venue); }

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

    if (!sidecar.views.empty()) {
        QJsonArray views;
        for (const auto& v : sidecar.views) views.append(viewToJson(v));
        root[QStringLiteral("views")] = views;
    }
    return root;
}

void sidecarFromJson(const QJsonObject& root, core::Sidecar& out) {
    out.views.clear();
    for (const auto& v : root.value(QStringLiteral("views")).toArray())
        if (auto view = viewFromJson(v)) out.views.push_back(std::move(*view));

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
        out.venue = venueFromJson(root.value(QStringLiteral("venue")).toObject());
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

void renameSidecarIds(core::Sidecar& sidecar, const QHash<QString, QString>& renamed) {
    if (renamed.isEmpty()) return;
    const auto rename = [&](QString& id) {
        const auto it = renamed.constFind(id);
        if (it != renamed.constEnd()) id = it.value();
    };
    for (auto& v : sidecar.views)
        if (v.sheets)
            for (QString& id : *v.sheets) rename(id);
    for (auto& l : sidecar.anchoredLabels) rename(l.targetId);
    for (auto& m : sidecar.modules) {
        QSet<QString> members;
        for (QString id : m.memberIds) {
            rename(id);
            members.insert(id);
        }
        m.memberIds = members;
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
