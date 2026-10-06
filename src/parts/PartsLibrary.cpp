#include "PartsLibrary.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QtMath>
#include <QPainter>
#include <QSet>
#include <QTransform>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::parts {

namespace {

// Split a filename stem like "TS_TRACK18S.8" into ("TS_TRACK18S", "8"),
// or "table96x190" into ("table96x190", ""). Trailing dot is rejected.
std::pair<QString, QString> splitPartKey(const QString& baseName) {
    const int dot = baseName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == baseName.size() - 1) {
        return { baseName, QString() };
    }
    return { baseName.left(dot), baseName.mid(dot + 1) };
}

void readDescriptions(QXmlStreamReader& r, QList<PartDescription>& out) {
    while (r.readNextStartElement()) {
        PartDescription d;
        d.language = r.name().toString();
        d.text = r.readElementText().trimmed();
        if (!d.language.isEmpty()) out.push_back(std::move(d));
    }
}

QPointF readPositionBlock(QXmlStreamReader& r) {
    QPointF p;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("x")) p.setX(r.readElementText().toDouble());
        else if (n == QStringLiteral("y")) p.setY(r.readElementText().toDouble());
        else r.skipCurrentElement();
    }
    return p;
}

void readConnexionList(QXmlStreamReader& r, QList<PartConnectionPoint>& out) {
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("connexion")) { r.skipCurrentElement(); continue; }
        PartConnectionPoint c;
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if      (n == QStringLiteral("type"))     c.type = r.readElementText().trimmed();
            else if (n == QStringLiteral("position")) c.position = readPositionBlock(r);
            else if (n == QStringLiteral("angle"))    c.angleDegrees = r.readElementText().toDouble();
            else if (n == QStringLiteral("electricPlug")) c.electricPlug = r.readElementText().toInt();
            else if (n == QStringLiteral("nextConnexionPreference")) c.nextPreferredIndex = r.readElementText().toInt();
            else r.skipCurrentElement();
        }
        out.push_back(std::move(c));
    }
}

void readSubPartList(QXmlStreamReader& r, QList<PartSubPart>& out) {
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("SubPart")) { r.skipCurrentElement(); continue; }
        PartSubPart sp;
        sp.subKey = r.attributes().value(QStringLiteral("id")).toString();
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if      (n == QStringLiteral("position")) sp.position = readPositionBlock(r);
            else if (n == QStringLiteral("angle"))    sp.angleDegrees = r.readElementText().toDouble();
            else r.skipCurrentElement();
        }
        out.push_back(std::move(sp));
    }
}

// BlueBrick's readGroupConnectionPreferenceListTag: <nextIndex from="i">j</nextIndex>.
void readGroupConnectionPreferences(QXmlStreamReader& r, QHash<int, int>& out) {
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("nextIndex")) {
            r.skipCurrentElement();
            continue;
        }
        bool okFrom = false, okTo = false;
        const int from = r.attributes().value(QStringLiteral("from")).toInt(&okFrom);
        const int to = r.readElementText().trimmed().toInt(&okTo);
        if (okFrom && okTo && from >= 0 && to >= 0) out.insert(from, to);
    }
}

void readLDrawRemap(QXmlStreamReader& r, PartMetadata& out) {
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("Angle"))           out.ldrawAngle = r.readElementText().toDouble();
        else if (n == QStringLiteral("Translation"))     out.ldrawTranslation = readPositionBlock(r);
        else if (n == QStringLiteral("PreferredHeight")) out.ldrawPreferredHeight = r.readElementText().toDouble();
        else if (n == QStringLiteral("SleeperID"))       out.ldrawSleeper = r.readElementText().trimmed().toUpper();
        else if (n == QStringLiteral("Alias"))           out.ldrawAlias = r.readElementText().trimmed().toUpper();
        else r.skipCurrentElement();
    }
    // Vanilla: a sleeper without a colour is black.
    if (!out.ldrawSleeper.isEmpty() && !out.ldrawSleeper.contains(QLatin1Char('.')))
        out.ldrawSleeper += QStringLiteral(".0");
}

void readHull(QXmlStreamReader& r, QList<QPointF>& out) {
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("point")) { r.skipCurrentElement(); continue; }
        // BlueBrick shifts hull points to the pixel centre.
        out << readPositionBlock(r) + QPointF(0.5, 0.5);
    }
}

void readTrackDesigner(QXmlStreamReader& r, PartMetadata::TrackDesigner& td) {
    const auto readId = [&]() {
        const QString registry = r.attributes().value(QStringLiteral("registry")).toString();
        const int id = r.readElementText().trimmed().toInt();
        if (registry.isEmpty() || registry == QLatin1String("default")) {
            td.defaultId = id;
        } else {
            td.registryIds.insert(registry, id);
            if (td.defaultId == 0) td.defaultId = id;
        }
    };
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (n == QStringLiteral("ID")) {
            readId();
        } else if (n == QStringLiteral("IDList")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("ID")) readId();
                else r.skipCurrentElement();
            }
        } else if (n == QStringLiteral("Flag")) {
            td.flags = r.readElementText().trimmed().toInt();
        } else if (n == QStringLiteral("HasSeveralGeometries")) {
            td.hasSeveralPorts = r.readElementText().trimmed() == QLatin1String("true");
        } else if (n == QStringLiteral("TDBitmapList")) {
            while (r.readNextStartElement()) {
                if (r.name() != QStringLiteral("TDBitmap")) { r.skipCurrentElement(); continue; }
                PartMetadata::TrackDesignerPort port;
                while (r.readNextStartElement()) {
                    const auto m = r.name();
                    if      (m == QStringLiteral("BBConnexionPointIndex")) port.bbConnectionIndex = r.readElementText().toInt();
                    else if (m == QStringLiteral("Type"))                  port.type = r.readElementText().toInt();
                    else if (m == QStringLiteral("AngleBetweenTDandBB"))   port.angleDifference = r.readElementText().toFloat();
                    else r.skipCurrentElement();
                }
                td.ports << port;
            }
        } else {
            r.skipCurrentElement();
        }
    }
}

void readFourDBrix(QXmlStreamReader& r, PartMetadata::FourDBrix& fd) {
    using Type = PartMetadata::FourDBrix::Type;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (n == QStringLiteral("PartType")) {
            const QString t = r.readElementText().trimmed().toUpper();
            fd.type = t == QLatin1String("TABLE") ? Type::Table
                    : t == QLatin1String("BASEPLATE") ? Type::Baseplate
                    : t == QLatin1String("STRUCTURE") ? Type::Structure : Type::Segment;
        }
        else if (n == QStringLiteral("PartName"))                    fd.partName = r.readElementText().trimmed();
        else if (n == QStringLiteral("OrientationDifference"))       fd.orientationDifference = r.readElementText().toFloat();
        else if (n == QStringLiteral("ConnectionIndexUsedAsOrigin")) fd.originConnection = r.readElementText().toInt();
        else r.skipCurrentElement();
    }
}

void readOldNames(QXmlStreamReader& r, QStringList& out) {
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("OldName")) out << r.readElementText().trimmed();
        else r.skipCurrentElement();
    }
}

void buildElectricCircuits(PartMetadata& meta) {
    const int n = meta.connections.size();
    for (int i = 0; i < n - 1; ++i) {
        if (meta.connections[i].electricPlug == 0) continue;
        for (int j = i + 1; j < n; ++j) {
            if (meta.connections[i].electricPlug == -meta.connections[j].electricPlug) {
                const QPointF d = meta.connections[j].position - meta.connections[i].position;
                ElectricCircuit ec;
                ec.index1 = i;
                ec.index2 = j;
                ec.distanceStuds = static_cast<float>(std::hypot(d.x(), d.y()));
                meta.electricCircuits.append(ec);
            }
        }
    }
}

bool parsePartXml(const QString& xmlPath, PartMetadata& out) {
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QXmlStreamReader r(&f);
    while (r.readNextStartElement()) {
        const auto root = r.name();
        if (root != QStringLiteral("part") && root != QStringLiteral("group")) {
            r.skipCurrentElement();
            continue;
        }
        out.kind = (root == QStringLiteral("group")) ? PartKind::Group : PartKind::Leaf;
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if      (n == QStringLiteral("Author"))      out.author = r.readElementText().trimmed();
            else if (n == QStringLiteral("SortingKey")) out.sortingKey = r.readElementText().trimmed();
            else if (n == QStringLiteral("Description")) readDescriptions(r, out.descriptions);
            else if (n == QStringLiteral("ConnexionList")) readConnexionList(r, out.connections);
            else if (n == QStringLiteral("SubPartList"))   readSubPartList(r, out.subparts);
            else if (n == QStringLiteral("GroupConnectionPreferenceList"))
                readGroupConnectionPreferences(r, out.groupNextPreferred);
            else if (n == QStringLiteral("CanUngroup"))
                out.canUngroup = r.readElementText().trimmed().compare(QLatin1String("false"), Qt::CaseInsensitive) != 0;
            else if (n == QStringLiteral("LDraw"))         readLDrawRemap(r, out);
            else if (n == QStringLiteral("OldNameList"))   readOldNames(r, out.oldNames);
            else if (n == QStringLiteral("hull"))          readHull(r, out.xmlHullPx);
            else if (n == QStringLiteral("TrackDesigner")) {
                PartMetadata::TrackDesigner td;
                readTrackDesigner(r, td);
                if (td.defaultId != 0 || !td.registryIds.isEmpty()) out.trackDesigner = td;
            }
            else if (n == QStringLiteral("ImportSource")) {
                PartMetadata::ImportSource src;
                while (r.readNextStartElement()) {
                    const auto m = r.name();
                    if (m == QStringLiteral("SourcePath")) src.path = r.readElementText().trimmed();
                    else if (m == QStringLiteral("SourceModified"))
                        src.modified = QDateTime::fromString(r.readElementText().trimmed(), Qt::ISODateWithMs);
                    else if (m == QStringLiteral("QuarterTurns")) src.quarterTurns = r.readElementText().toInt();
                    else if (m == QStringLiteral("DroppedConnection")) {
                        src.droppedConnections << QPointF(r.attributes().value(QStringLiteral("x")).toDouble(),
                                                          r.attributes().value(QStringLiteral("y")).toDouble());
                        r.skipCurrentElement();
                    }
                    else r.skipCurrentElement();
                }
                if (!src.path.isEmpty()) out.importSource = src;
            }
            else if (n == QStringLiteral("FourDBrix")) {
                PartMetadata::FourDBrix fd;
                readFourDBrix(r, fd);
                out.fourDBrix = fd;
            }
            else if (n == QStringLiteral("PixelsPerStud")) {
                bool ok = false;
                const int v = r.readElementText().trimmed().toInt(&ok);
                if (ok && v >= 4 && v <= 256) out.pxPerStud = v;
            }
            else r.skipCurrentElement();
        }
        return !r.hasError();
    }
    return false;
}

}

void PartsLibrary::addSearchPath(const QString& path) {
    if (!searchPaths_.contains(path)) searchPaths_.push_back(path);
}

QString PartsLibrary::scanFile(const QString& xmlPath) {
    QFileInfo info(xmlPath);
    if (!info.exists() || !info.isFile()) return {};

    // Strip ".xml". Then also strip ".set" if present (composite/group parts
    // use the ".set.xml" convention in BlueBrickParts). Remaining string is
    // "<PartNumber>.<ColorCode>".
    QString stem = info.completeBaseName();  // filename without ".xml"
    if (stem.endsWith(QStringLiteral(".set"), Qt::CaseInsensitive)) {
        stem.chop(4);
    }

    const auto [partNum, colorCode] = splitPartKey(stem);
    if (partNum.isEmpty()) return {};

    PartMetadata meta;
    meta.partNumber = partNum;
    meta.colorCode  = colorCode;
    meta.xmlFilePath = xmlPath;

    if (!parsePartXml(xmlPath, meta)) return {};
    buildElectricCircuits(meta);

    // Sibling sprite. BlueBrickParts ships 8 px/stud .gif files. Our
    // importer writes a high-resolution .png (declared via
    // <PixelsPerStud>) plus an 8 px/stud .gif for vanilla BlueBrick, so
    // when the XML declares a higher resolution prefer the .png; a .gif
    // is always 8 px/stud whatever the XML says. .jpg/.jpeg cover
    // hand-added parts.
    const QString stemPath = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
    const bool hiRes = meta.pxPerStud != 8;
    const QStringList exts = hiRes
        ? QStringList{ QStringLiteral(".png"), QStringLiteral(".gif"),
                       QStringLiteral(".jpg"), QStringLiteral(".jpeg") }
        : QStringList{ QStringLiteral(".gif"), QStringLiteral(".png"),
                       QStringLiteral(".jpg"), QStringLiteral(".jpeg") };
    for (const QString& ext : exts) {
        const QString candidate = stemPath + ext;
        if (QFile::exists(candidate)) {
            meta.gifFilePath = candidate;
            break;
        }
    }
    if (meta.gifFilePath.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) meta.pxPerStud = 8;
    // One-time migration: an older import bug wrote PNG bytes
    // to "<stem>.gif.png" when GIF support was missing in Qt.
    // Rename those to "<stem>.png" so the parts panel finally
    // picks them up. Only fires when no real sibling was
    // found above.
    if (meta.gifFilePath.isEmpty()) {
        const QString legacy = stemPath + QStringLiteral(".gif.png");
        if (QFile::exists(legacy)) {
            const QString fixed = stemPath + QStringLiteral(".png");
            if (!QFile::exists(fixed) && QFile::rename(legacy, fixed)) {
                meta.gifFilePath = fixed;
            } else {
                meta.gifFilePath = legacy;  // accept as-is
            }
        }
    }

    // Library keys are the full stem (case-folded) so lookup matches
    // both "TABLE96X190" and "3811.1" naturally — the stored key
    // always includes the color suffix when the filename has one.
    const QString key = colorCode.isEmpty()
        ? partNum.toLower()
        : QStringLiteral("%1.%2").arg(partNum, colorCode).toLower();
    if (index_.contains(key)) return {};
    for (const QString& old : meta.oldNames) renamed_.insert(old.toLower(), key);
    if (meta.trackDesigner) {
        QSet<int> ids{ meta.trackDesigner->defaultId };
        for (int id : meta.trackDesigner->registryIds) ids.insert(id);
        for (int id : ids) if (id != 0) trackDesignerIds_[id].append(key);
    }
    if (meta.fourDBrix && !meta.fourDBrix->partName.isEmpty()) {
        // BlueBrick keeps the first part it reads (files in name order).
        QString& owner = fourDBrixNames_[meta.fourDBrix->partName];
        if (owner.isEmpty() || key < owner) owner = key;
    }
    index_.insert(key, meta);
    return key;
}

int PartsLibrary::scan() {
    int added = 0;
    for (const QString& root : std::as_const(searchPaths_)) {
        QDirIterator it(root, { QStringLiteral("*.xml") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (!scanFile(it.next()).isEmpty()) ++added;
        }
    }
    return added;
}

std::optional<PartMetadata> PartsLibrary::metadata(const QString& key) const {
    auto it = index_.constFind(key.toLower());
    if (it == index_.constEnd()) {
        const auto renamed = renamed_.constFind(key.toLower());
        if (renamed == renamed_.constEnd()) return std::nullopt;
        it = index_.constFind(renamed.value());
        if (it == index_.constEnd()) return std::nullopt;
    }
    return it.value();
}

std::optional<PartsLibrary::Footprint> PartsLibrary::footprint(const QString& key, double orientationDegrees) {
    const auto meta = metadata(key);
    if (!meta) return std::nullopt;
    const QPixmap pm = pixmap(key);
    if (pm.isNull()) return std::nullopt;
    const float pxPerStud = meta->pxPerStud > 0 ? static_cast<float>(meta->pxPerStud) : 8.0f;

    // Same float arithmetic as LayerBrick.Brick.updateImage().
    const float lastX = static_cast<float>(pm.width()) - 0.5f;
    const float lastY = static_cast<float>(pm.height()) - 0.5f;
    const QPointF box[4] = { { 0.5, 0.5 }, { lastX, 0.5 }, { lastX, lastY }, { 0.5, lastY } };
    const double rad = qDegreesToRadians(orientationDegrees);
    const float c = static_cast<float>(std::cos(rad)), sn = static_cast<float>(std::sin(rad));
    struct MinMax { float minX, minY, maxX, maxY; };
    const auto bounds = [&](auto begin, auto end) {
        MinMax mm{ 1e30f, 1e30f, -1e30f, -1e30f };
        for (auto it = begin; it != end; ++it) {
            const float x = static_cast<float>(it->x()), y = static_cast<float>(it->y());
            const float rx = x * c - y * sn, ry = x * sn + y * c;
            mm.minX = std::min(mm.minX, rx); mm.maxX = std::max(mm.maxX, rx);
            mm.minY = std::min(mm.minY, ry); mm.maxY = std::max(mm.maxY, ry);
        }
        return mm;
    };
    const MinMax bb = bounds(std::begin(box), std::end(box));
    Footprint fp;
    if (meta->xmlHullPx.isEmpty()) {
        fp.size = QSizeF((bb.maxX - bb.minX + 1.0f) / pxPerStud, (bb.maxY - bb.minY + 1.0f) / pxPerStud);
        fp.imageCorner = QPointF((0.5f - bb.minX) / pxPerStud, (0.5f - bb.minY) / pxPerStud);
        return fp;
    }
    const MinMax hull = bounds(meta->xmlHullPx.cbegin(), meta->xmlHullPx.cend());
    const float ox = ((bb.maxX - hull.maxX) + (bb.minX - hull.minX)) * 0.5f / pxPerStud;
    const float oy = ((bb.maxY - hull.maxY) + (bb.minY - hull.minY)) * 0.5f / pxPerStud;
    fp.imageOffset = QPointF(ox, oy);
    fp.size = QSizeF((hull.maxX - hull.minX + 1.0f) / pxPerStud, (hull.maxY - hull.minY + 1.0f) / pxPerStud);
    fp.imageCorner = QPointF((0.5f - hull.minX) / pxPerStud, (0.5f - hull.minY) / pxPerStud);
    return fp;
}

QPointF PartsLibrary::imageOffset(const QString& key, double orientationDegrees) {
    const QString lk = key.toLower();
    auto it = index_.constFind(lk);
    if (it == index_.constEnd()) it = index_.constFind(renamed_.value(lk));
    if (it == index_.constEnd() || it->xmlHullPx.isEmpty()) return {};
    const auto fp = footprint(key, orientationDegrees);
    return fp ? fp->imageOffset : QPointF();
}

QString PartsLibrary::partForTrackDesignerId(int tdId, const QString& registry) const {
    QString best, fallback, other;
    for (const QString& key : trackDesignerIds_.value(tdId)) {
        const auto it = index_.constFind(key);
        if (it == index_.constEnd() || !it->trackDesigner) continue;
        const auto& td = *it->trackDesigner;
        if (td.registryIds.value(registry, 0) == tdId) best = key;
        else if (td.defaultId == tdId) fallback = key;
        else other = key;
    }
    return !best.isEmpty() ? best : !fallback.isEmpty() ? fallback : other;
}

QString PartsLibrary::partForFourDBrixName(const QString& name) const {
    return fourDBrixNames_.value(name);
}

QString PartsLibrary::canonicalKey(const QString& key) const {
    const QString lk = key.toLower();
    if (index_.contains(lk)) return lk;
    return renamed_.value(lk);
}

QStringList PartsLibrary::keys() const {
    return index_.keys();
}

QPixmap PartsLibrary::pixmap(const QString& key) {
    const QString lk = key.toLower();
    auto cached = pixmapCache_.constFind(lk);
    if (cached != pixmapCache_.constEnd()) return cached.value();
    auto meta = metadata(lk);
    if (!meta) return {};

    // Prefer a sibling sprite on disk — vanilla BlueBrick sets like
    // Castle/3739-1.set.gif or Town/00-1.set.gif ship with one and the
    // authored thumbnail is more tightly framed than what we can synthesize.
    if (!meta->gifFilePath.isEmpty()) {
        QPixmap pm;
        pm.load(meta->gifFilePath);
        pixmapCache_.insert(lk, pm);
        return pm;
    }

    // Fallback for sets without a companion image — BrickTracks/TrixBrix/
    // 4DBrix and our own onSaveSelectionAsSet write only the .set.xml.
    // Composite the icon by painting each subpart's pixmap at its set-local
    // position and angle. As when placing a set, sp.position is the
    // subpart's displayArea centre, so the sprite centre is imageOffset()
    // away from it.
    // Output is at 8 px/stud (kPixelsPerStud), matching how leaf-part GIFs
    // are authored — PartsBrowser scales it down to icon size from there.
    if (meta->kind == PartKind::Group && !meta->subparts.isEmpty()) {
        constexpr double kPxPerStud = 8.0;

        struct Placed {
            QPixmap pm;
            double  angleDeg = 0.0;
            QPointF centreStuds;     // image bbox centre in set-local studs
            double  scale = 1.0;     // kPxPerStud / authoredPxPerStud
        };
        QList<Placed> placed;
        placed.reserve(meta->subparts.size());

        double minX = std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();

        for (const PartSubPart& sp : meta->subparts) {
            QPixmap sub = pixmap(sp.subKey);
            if (sub.isNull()) continue;

            auto subMeta = metadata(sp.subKey);
            const double subPxPerStud = (subMeta && subMeta->pxPerStud > 0)
                ? static_cast<double>(subMeta->pxPerStud) : kPxPerStud;
            const double s = kPxPerStud / subPxPerStud;

            double angle = std::fmod(sp.angleDegrees, 360.0);
            if (angle >  180.0) angle -= 360.0;
            if (angle <= -180.0) angle += 360.0;

            const QPointF off = imageOffset(sp.subKey, angle);  // sp.position is the displayArea centre
            const QPointF centreStuds = sp.position + off;

            const double wStuds = (sub.width()  * s) / kPxPerStud;
            const double hStuds = (sub.height() * s) / kPxPerStud;
            const double r = angle * M_PI / 180.0;
            const double cs = std::abs(std::cos(r));
            const double sn = std::abs(std::sin(r));
            const double rotW = wStuds * cs + hStuds * sn;
            const double rotH = wStuds * sn + hStuds * cs;

            minX = std::min(minX, centreStuds.x() - rotW / 2.0);
            minY = std::min(minY, centreStuds.y() - rotH / 2.0);
            maxX = std::max(maxX, centreStuds.x() + rotW / 2.0);
            maxY = std::max(maxY, centreStuds.y() + rotH / 2.0);

            placed.push_back({ sub, angle, centreStuds, s });
        }

        QPixmap composite;
        if (!placed.isEmpty() && std::isfinite(minX)) {
            // 1-stud margin so anti-aliased edges don't get clipped.
            constexpr double kMarginStuds = 1.0;
            const double widthStuds  = (maxX - minX) + 2.0 * kMarginStuds;
            const double heightStuds = (maxY - minY) + 2.0 * kMarginStuds;
            const int wPx = std::max(1, static_cast<int>(std::ceil(widthStuds  * kPxPerStud)));
            const int hPx = std::max(1, static_cast<int>(std::ceil(heightStuds * kPxPerStud)));

            QImage img(wPx, hPx, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            {
                QPainter painter(&img);
                painter.setRenderHint(QPainter::Antialiasing,            true);
                painter.setRenderHint(QPainter::SmoothPixmapTransform,   true);
                for (const Placed& p : placed) {
                    const double cx = (p.centreStuds.x() - minX + kMarginStuds) * kPxPerStud;
                    const double cy = (p.centreStuds.y() - minY + kMarginStuds) * kPxPerStud;
                    QTransform t;
                    t.translate(cx, cy);
                    t.rotate(p.angleDeg);
                    t.scale(p.scale, p.scale);
                    t.translate(-p.pm.width() / 2.0, -p.pm.height() / 2.0);
                    painter.setTransform(t);
                    painter.drawPixmap(0, 0, p.pm);
                }
            }
            composite = QPixmap::fromImage(img);
        }
        pixmapCache_.insert(lk, composite);
        return composite;
    }

    return {};
}

namespace {

// Andrew's monotone-chain convex hull. Input points may contain duplicates;
// output is a simple polygon ordered counter-clockwise in image coords
// (y-down), first point repeated at the end is NOT added — callers treat
// it as implicitly closed.
QVector<QPointF> convexHull(QVector<QPointF> pts) {
    if (pts.size() < 3) return pts;
    std::sort(pts.begin(), pts.end(),
              [](QPointF a, QPointF b){
                  return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x();
              });
    const int n = pts.size();
    QVector<QPointF> hull;
    hull.reserve(2 * n);
    auto cross = [](QPointF O, QPointF A, QPointF B){
        return (A.x() - O.x()) * (B.y() - O.y()) - (A.y() - O.y()) * (B.x() - O.x());
    };
    // Lower hull.
    for (int i = 0; i < n; ++i) {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }
    // Upper hull.
    const int lower = hull.size() + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (hull.size() >= lower && cross(hull[hull.size() - 2], hull.back(), pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }
    hull.pop_back();  // first point duplicated at end
    return hull;
}

}  // namespace

QPolygonF PartsLibrary::hullPolygonStuds(const QString& key) {
    const QString lk = key.toLower();
    auto cached = hullCache_.constFind(lk);
    if (cached != hullCache_.constEnd()) return cached.value();

    QPixmap pm = pixmap(lk);
    if (pm.isNull()) { hullCache_.insert(lk, {}); return {}; }

    const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    if (w < 2 || h < 2) { hullCache_.insert(lk, {}); return {}; }

    // Sample opaque pixels on a coarse stride to keep hull computation fast
    // for big sprites. 2px stride is enough granularity for silhouettes at
    // BlueBrick's 8 px/stud scale (~¼ stud). Edge pixels are always kept
    // so the hull touches the exact outline.
    constexpr int kStride = 2;
    constexpr int kAlphaThreshold = 32;
    QVector<QPointF> pts;
    pts.reserve((w * h) / (kStride * kStride));
    for (int y = 0; y < h; y += kStride) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; x += kStride) {
            if (qAlpha(line[x]) >= kAlphaThreshold) {
                pts.append(QPointF(x, y));
            }
        }
    }
    if (pts.size() < 3) { hullCache_.insert(lk, {}); return {}; }

    // Compute hull in pixel coords, then translate to part-local coords
    // centred on the pixmap (so hullPolygonStuds aligns with the brick
    // item's setPos(centerPx) + setOffset(-w/2,-h/2) rendering model)
    // and divide by 8 px/stud.
    const auto hullPx = convexHull(pts);
    QPolygonF result;
    result.reserve(hullPx.size());
    constexpr double kPxPerStud = 8.0;
    for (const QPointF& p : hullPx) {
        result.append(QPointF((p.x() - w / 2.0) / kPxPerStud,
                              (p.y() - h / 2.0) / kPxPerStud));
    }
    hullCache_.insert(lk, result);
    return result;
}

void PartsLibrary::forget(const QString& key) {
    const QString lk = key.toLower();
    for (auto& keys : trackDesignerIds_) keys.removeAll(lk);
    for (auto it = fourDBrixNames_.begin(); it != fourDBrixNames_.end();)
        it = it.value() == lk ? fourDBrixNames_.erase(it) : std::next(it);
    index_.remove(lk);
    pixmapCache_.remove(lk);
    hullCache_.remove(lk);
}

bool PartsLibrary::pixmapTried(const QString& key) const { return pixmapCache_.contains(key.toLower()); }

void PartsLibrary::forgetPixmap(const QString& key) {
    const QString lk = key.toLower();
    pixmapCache_.remove(lk);
    hullCache_.remove(lk);
}

void PartsLibrary::clear() {
    index_.clear();
    renamed_.clear();
    trackDesignerIds_.clear();
    fourDBrixNames_.clear();
    pixmapCache_.clear();
    hullCache_.clear();
}

}
