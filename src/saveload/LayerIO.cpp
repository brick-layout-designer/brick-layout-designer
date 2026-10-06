#include "LayerIO.h"

#include "../core/Groups.h"

#include "XmlPrimitives.h"

#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"

#include <QXmlStreamAttributes>

namespace bld::saveload {

namespace {

// ---------- Layer-common header (Name/Visible/Transparency/HullProperties) ----------

void readCommonField(QXmlStreamReader& r, core::Layer& layer, int dataVersion, QStringView n) {
    if (n == QStringLiteral("Name"))          layer.name = xml::readTextElement(r);
    else if (n == QStringLiteral("Visible"))  layer.visible = xml::readBoolElement(r);
    else if (n == QStringLiteral("Transparency") && dataVersion > 4) layer.transparency = xml::readIntElement(r);
    else if (n == QStringLiteral("HullProperties") && dataVersion >= 9) {
        layer.hull.displayHulls = r.attributes().value(QStringLiteral("isVisible"))
                                      .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        while (r.readNextStartElement()) {
            const auto hn = r.name();
            if      (hn == QStringLiteral("hullColor"))     layer.hull.color     = xml::readColor(r);
            else if (hn == QStringLiteral("hullThickness")) layer.hull.thickness = xml::readIntElement(r);
            else r.skipCurrentElement();
        }
    } else {
        r.skipCurrentElement();
    }
}

bool isCommonField(QStringView n, int dataVersion) {
    return n == QStringLiteral("Name")
        || n == QStringLiteral("Visible")
        || (n == QStringLiteral("Transparency") && dataVersion > 4)
        || (n == QStringLiteral("HullProperties") && dataVersion >= 9);
}

void writeLayerCommonHeader(QXmlStreamWriter& w, const core::Layer& layer, const QString& typeAttr) {
    w.writeStartElement(QStringLiteral("Layer"));
    w.writeAttribute(QStringLiteral("type"), typeAttr);
    w.writeAttribute(QStringLiteral("id"),   layer.guid);
    xml::writeTextElement(w, QStringLiteral("Name"),    layer.name);
    xml::writeBoolElement(w, QStringLiteral("Visible"), layer.visible);
    xml::writeIntElement (w, QStringLiteral("Transparency"), layer.transparency);
    w.writeStartElement(QStringLiteral("HullProperties"));
    w.writeAttribute(QStringLiteral("isVisible"), xml::formatBool(layer.hull.displayHulls));
    xml::writeColor     (w, QStringLiteral("hullColor"),     layer.hull.color);
    xml::writeIntElement(w, QStringLiteral("hullThickness"), layer.hull.thickness);
    w.writeEndElement();
}

// ---------- LayerItem-common (DisplayArea + MyGroup) ----------

void readItemCommon(QXmlStreamReader& r, core::LayerItem& item, int dataVersion) {
    // The reader is positioned just-inside the item start tag. Read DisplayArea then MyGroup.
    if (r.readNextStartElement() && r.name() == QStringLiteral("DisplayArea")) {
        item.displayArea = xml::readRectF(r);
    }
    if (dataVersion > 4) {
        if (r.readNextStartElement() && r.name() == QStringLiteral("MyGroup")) {
            item.myGroupId = xml::readTextElement(r);
        }
    }
}

void writeItemCommon(QXmlStreamWriter& w, const core::LayerItem& item) {
    xml::writeRectF(w, QStringLiteral("DisplayArea"), item.displayArea);
    xml::writeTextElement(w, QStringLiteral("MyGroup"), item.myGroupId);
}

// ---------- <Groups> list at the tail of a layer ----------

void readGroupsTail(QXmlStreamReader& r, std::vector<core::Group>& groups) {
    // Expect current position: we've just consumed the items list and the next
    // readNextStartElement() has returned true with name == "Groups".
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("Group")) {
            r.skipCurrentElement();
            continue;
        }
        core::Group g;
        g.guid = r.attributes().value(QStringLiteral("id")).toString();
        while (r.readNextStartElement()) {
            const auto gn = r.name();
            if      (gn == QStringLiteral("PartNumber")) g.partNumber = xml::readTextElement(r);
            else if (gn == QStringLiteral("MyGroup"))    g.myGroupId  = xml::readTextElement(r);
            else r.skipCurrentElement();
        }
        groups.push_back(std::move(g));
    }
}

void writeGroupsTail(QXmlStreamWriter& w, const std::vector<core::Group>& groups) {
    w.writeStartElement(QStringLiteral("Groups"));
    for (const auto& g : groups) {
        w.writeStartElement(QStringLiteral("Group"));
        w.writeAttribute(QStringLiteral("id"), g.guid);
        xml::writeTextElement(w, QStringLiteral("PartNumber"), g.partNumber);
        xml::writeTextElement(w, QStringLiteral("MyGroup"),    g.myGroupId);
        w.writeEndElement();
    }
    w.writeEndElement();
}

// ---------- LayerGrid ----------

std::unique_ptr<core::LayerGrid> readLayerGrid(QXmlStreamReader& r, int dataVersion) {
    auto grid = std::make_unique<core::LayerGrid>();
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (isCommonField(n, dataVersion)) { readCommonField(r, *grid, dataVersion, n); continue; }
        if      (n == QStringLiteral("GridColor"))           grid->gridColor        = xml::readColor(r);
        else if (n == QStringLiteral("GridThickness"))       grid->gridThickness    = xml::readFloatElement(r);
        else if (n == QStringLiteral("SubGridColor"))        grid->subGridColor     = xml::readColor(r);
        else if (n == QStringLiteral("SubGridThickness"))    grid->subGridThickness = xml::readFloatElement(r);
        else if (n == QStringLiteral("GridSizeInStud"))      grid->gridSizeInStud   = xml::readIntElement(r);
        else if (n == QStringLiteral("SubDivisionNumber"))   grid->subDivisionNumber = std::max(xml::readIntElement(r), 2);
        else if (n == QStringLiteral("DisplayGrid"))         grid->displayGrid       = xml::readBoolElement(r);
        else if (n == QStringLiteral("DisplaySubGrid"))      grid->displaySubGrid    = xml::readBoolElement(r);
        else if (n == QStringLiteral("DisplayCellIndex"))    grid->displayCellIndex  = xml::readBoolElement(r);
        else if (n == QStringLiteral("CellIndexFont"))       grid->cellIndexFont     = xml::readFont(r, dataVersion);
        else if (n == QStringLiteral("CellIndexColor"))      grid->cellIndexColor    = xml::readColor(r);
        else if (n == QStringLiteral("CellIndexColumnType")) grid->cellIndexColumnType = static_cast<core::CellIndexType>(xml::readIntElement(r));
        else if (n == QStringLiteral("CellIndexRowType"))    grid->cellIndexRowType    = static_cast<core::CellIndexType>(xml::readIntElement(r));
        else if (n == QStringLiteral("CellIndexCorner"))     grid->cellIndexCorner     = xml::readPoint(r);
        else r.skipCurrentElement();
    }
    return grid;
}

void writeLayerGrid(QXmlStreamWriter& w, const core::LayerGrid& g) {
    writeLayerCommonHeader(w, g, QStringLiteral("grid"));
    xml::writeColor       (w, QStringLiteral("GridColor"),        g.gridColor);
    xml::writeFloatElement(w, QStringLiteral("GridThickness"),    g.gridThickness);
    xml::writeColor       (w, QStringLiteral("SubGridColor"),     g.subGridColor);
    xml::writeFloatElement(w, QStringLiteral("SubGridThickness"), g.subGridThickness);
    xml::writeIntElement  (w, QStringLiteral("GridSizeInStud"),    g.gridSizeInStud);
    xml::writeIntElement  (w, QStringLiteral("SubDivisionNumber"), g.subDivisionNumber);
    xml::writeBoolElement (w, QStringLiteral("DisplayGrid"),       g.displayGrid);
    xml::writeBoolElement (w, QStringLiteral("DisplaySubGrid"),    g.displaySubGrid);
    xml::writeBoolElement (w, QStringLiteral("DisplayCellIndex"),  g.displayCellIndex);
    xml::writeFont        (w, QStringLiteral("CellIndexFont"),     g.cellIndexFont);
    xml::writeColor       (w, QStringLiteral("CellIndexColor"),    g.cellIndexColor);
    xml::writeIntElement  (w, QStringLiteral("CellIndexColumnType"), static_cast<int>(g.cellIndexColumnType));
    xml::writeIntElement  (w, QStringLiteral("CellIndexRowType"),    static_cast<int>(g.cellIndexRowType));
    xml::writePoint       (w, QStringLiteral("CellIndexCorner"),     g.cellIndexCorner);
    w.writeEndElement();
}

// ---------- LayerBrick ----------

core::Brick readBrick(QXmlStreamReader& r, int dataVersion) {
    // Reader is at the <Brick> start element.
    core::Brick brick;
    brick.guid = r.attributes().value(QStringLiteral("id")).toString();
    readItemCommon(r, brick, dataVersion);
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("PartNumber"))                 brick.partNumber = xml::readTextElement(r);
        else if (n == QStringLiteral("Orientation"))                brick.orientation = xml::readFloatElement(r);
        else if (n == QStringLiteral("ActiveConnectionPointIndex")) brick.activeConnectionPointIndex = xml::readIntElement(r);
        else if (n == QStringLiteral("Altitude") && dataVersion >= 3) brick.altitude = xml::readFloatElement(r);
        else if (n == QStringLiteral("Connexions")) {
            while (r.readNextStartElement()) {
                if (r.name() != QStringLiteral("Connexion")) { r.skipCurrentElement(); continue; }
                core::ConnectionPoint cp;
                cp.guid = r.attributes().value(QStringLiteral("id")).toString();
                while (r.readNextStartElement()) {
                    if (r.name() == QStringLiteral("LinkedTo")) cp.linkedToId = xml::readTextElement(r);
                    else r.skipCurrentElement();
                }
                brick.connections.push_back(std::move(cp));
            }
        }
        else r.skipCurrentElement();
    }
    return brick;
}

void writeBrick(QXmlStreamWriter& w, const core::Brick& b) {
    w.writeStartElement(QStringLiteral("Brick"));
    w.writeAttribute(QStringLiteral("id"), b.guid);
    writeItemCommon(w, b);
    xml::writeTextElement (w, QStringLiteral("PartNumber"),                 b.partNumber);
    xml::writeFloatElement(w, QStringLiteral("Orientation"),                b.orientation);
    xml::writeIntElement  (w, QStringLiteral("ActiveConnectionPointIndex"), b.activeConnectionPointIndex);
    xml::writeFloatElement(w, QStringLiteral("Altitude"),                   b.altitude);
    w.writeStartElement(QStringLiteral("Connexions"));
    w.writeAttribute(QStringLiteral("count"), QString::number(b.connections.size()));
    for (const auto& cp : b.connections) {
        w.writeStartElement(QStringLiteral("Connexion"));
        w.writeAttribute(QStringLiteral("id"), cp.guid);
        xml::writeTextElement(w, QStringLiteral("LinkedTo"), cp.linkedToId);
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndElement();
}

std::unique_ptr<core::LayerBrick> readLayerBrick(QXmlStreamReader& r, int dataVersion) {
    auto out = std::make_unique<core::LayerBrick>();
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (isCommonField(n, dataVersion)) { readCommonField(r, *out, dataVersion, n); continue; }
        if      (n == QStringLiteral("DisplayBrickElevation") && dataVersion >= 9) out->displayBrickElevation = xml::readBoolElement(r);
        else if (n == QStringLiteral("Bricks")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("Brick")) out->bricks.push_back(readBrick(r, dataVersion));
                else r.skipCurrentElement();
            }
        } else if (n == QStringLiteral("Groups")) {
            readGroupsTail(r, out->groups);
        } else r.skipCurrentElement();
    }
    return out;
}

void writeLayerBrick(QXmlStreamWriter& w, const core::LayerBrick& b) {
    writeLayerCommonHeader(w, b, QStringLiteral("brick"));
    xml::writeBoolElement(w, QStringLiteral("DisplayBrickElevation"), b.displayBrickElevation);
    w.writeStartElement(QStringLiteral("Bricks"));
    for (const auto& brick : b.bricks) writeBrick(w, brick);
    w.writeEndElement();
    // A group with nothing under it any more (its parts were deleted) is
    // left out: BlueBrick only writes the groups of the items it has.
    const QSet<QString> empty = core::emptyGroups(b);
    std::vector<core::Group> kept;
    for (const auto& g : b.groups)
        if (!empty.contains(g.guid)) kept.push_back(g);
    writeGroupsTail(w, kept);
    w.writeEndElement();
}

// ---------- LayerText ----------

core::TextCell readTextCell(QXmlStreamReader& r, int dataVersion) {
    core::TextCell t;
    t.guid = r.attributes().value(QStringLiteral("id")).toString();
    readItemCommon(r, t, dataVersion);
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("Text"))          t.text = xml::readTextElement(r);
        else if (n == QStringLiteral("Orientation"))   t.orientation = xml::readFloatElement(r);
        else if (n == QStringLiteral("FontColor"))     t.fontColor = xml::readColor(r);
        else if (n == QStringLiteral("Font"))          t.font = xml::readFont(r, dataVersion);
        else if (n == QStringLiteral("TextAlignment")) {
            const auto s = xml::readTextElement(r);
            if      (s == QStringLiteral("Near")) t.alignment = core::TextAlignment::Near;
            else if (s == QStringLiteral("Far"))  t.alignment = core::TextAlignment::Far;
            else                                  t.alignment = core::TextAlignment::Center;
        } else r.skipCurrentElement();
    }
    return t;
}

QString textAlignmentToString(core::TextAlignment a) {
    switch (a) {
        case core::TextAlignment::Near:   return QStringLiteral("Near");
        case core::TextAlignment::Far:    return QStringLiteral("Far");
        case core::TextAlignment::Center: return QStringLiteral("Center");
    }
    return QStringLiteral("Center");
}

void writeTextCell(QXmlStreamWriter& w, const core::TextCell& t) {
    // Vanilla <TextCell> has no id attribute (unlike <Brick>). Upstream:
    // LayerTextCell.cs WriteXml writes only the start tag + base + fields.
    w.writeStartElement(QStringLiteral("TextCell"));
    writeItemCommon(w, t);
    xml::writeTextElement (w, QStringLiteral("Text"),        t.text);
    xml::writeFloatElement(w, QStringLiteral("Orientation"), t.orientation);
    xml::writeColor       (w, QStringLiteral("FontColor"),   t.fontColor);
    xml::writeFont        (w, QStringLiteral("Font"),        t.font);
    xml::writeTextElement (w, QStringLiteral("TextAlignment"), textAlignmentToString(t.alignment));
    w.writeEndElement();
}

std::unique_ptr<core::LayerText> readLayerText(QXmlStreamReader& r, int dataVersion) {
    auto out = std::make_unique<core::LayerText>();
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (isCommonField(n, dataVersion)) { readCommonField(r, *out, dataVersion, n); continue; }
        if (n == QStringLiteral("TextCells")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("TextCell")) out->textCells.push_back(readTextCell(r, dataVersion));
                else r.skipCurrentElement();
            }
        } else if (n == QStringLiteral("Groups")) {
            readGroupsTail(r, out->groups);
        } else r.skipCurrentElement();
    }
    return out;
}

void writeLayerText(QXmlStreamWriter& w, const core::LayerText& t) {
    writeLayerCommonHeader(w, t, QStringLiteral("text"));
    w.writeStartElement(QStringLiteral("TextCells"));
    for (const auto& cell : t.textCells) writeTextCell(w, cell);
    w.writeEndElement();
    writeGroupsTail(w, t.groups);
    w.writeEndElement();
}

// ---------- LayerArea ----------

std::unique_ptr<core::LayerArea> readLayerArea(QXmlStreamReader& r, int dataVersion) {
    auto out = std::make_unique<core::LayerArea>();
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (isCommonField(n, dataVersion)) { readCommonField(r, *out, dataVersion, n); continue; }
        if (n == QStringLiteral("Transparency") && dataVersion < 5) {
            // Pre-v5 placed Transparency in LayerArea body rather than the common header.
            out->transparency = xml::readIntElement(r);
        } else if (n == QStringLiteral("AreaCellSize")) {
            out->areaCellSizeInStud = xml::readIntElement(r);
        } else if (n == QStringLiteral("Areas")) {
            while (r.readNextStartElement()) {
                if (r.name() != QStringLiteral("Area")) { r.skipCurrentElement(); continue; }
                core::AreaCell cell;
                while (r.readNextStartElement()) {
                    const auto an = r.name();
                    if      (an == QStringLiteral("x")) cell.x = xml::readIntElement(r);
                    else if (an == QStringLiteral("y")) cell.y = xml::readIntElement(r);
                    else if (an == QStringLiteral("color")) {
                        // ARGB packed as signed int32 written in hex ("X" = uppercase hex in .NET).
                        const QString s = xml::readTextElement(r);
                        bool ok = false;
                        const auto u = static_cast<quint32>(s.toLongLong(&ok, 16));
                        cell.color = QColor::fromRgba(u);
                    }
                    else r.skipCurrentElement();
                }
                out->cells.push_back(cell);
            }
        } else r.skipCurrentElement();
    }
    return out;
}

void writeLayerArea(QXmlStreamWriter& w, const core::LayerArea& a) {
    writeLayerCommonHeader(w, a, QStringLiteral("area"));
    xml::writeIntElement(w, QStringLiteral("AreaCellSize"), a.areaCellSizeInStud);
    w.writeStartElement(QStringLiteral("Areas"));
    for (const auto& c : a.cells) {
        w.writeStartElement(QStringLiteral("Area"));
        xml::writeIntElement(w, QStringLiteral("x"), c.x);
        xml::writeIntElement(w, QStringLiteral("y"), c.y);
        const quint32 argb = (static_cast<quint32>(c.color.alpha()) << 24)
                           | (static_cast<quint32>(c.color.red())   << 16)
                           | (static_cast<quint32>(c.color.green()) << 8)
                           | static_cast<quint32>(c.color.blue());
        // Vanilla emits the color as int.ToString("X") (uppercase hex) for LayerArea
        // — verified in upstream LayerArea.cs WriteXml. Note the deliberate divergence
        // from the <IsKnownColor>/<Name> wrapper which uses lowercase hex.
        xml::writeTextElement(w, QStringLiteral("color"),
            QStringLiteral("%1").arg(argb, 0, 16).toUpper());
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndElement(); // Layer
}

// ---------- LayerRuler ----------

bool consumeRulerBaseField(QXmlStreamReader& r, core::RulerItemBase& base, int dataVersion, QStringView n) {
    if      (n == QStringLiteral("Color"))                base.color = xml::readColor(r);
    else if (n == QStringLiteral("LineThickness"))        base.lineThickness = xml::readFloatElement(r);
    else if (n == QStringLiteral("DisplayDistance"))      base.displayDistance = xml::readBoolElement(r);
    else if (n == QStringLiteral("DisplayUnit"))          base.displayUnit = xml::readBoolElement(r);
    else if (n == QStringLiteral("GuidelineColor"))       base.guidelineColor = xml::readColor(r);
    else if (n == QStringLiteral("GuidelineThickness"))   base.guidelineThickness = xml::readFloatElement(r);
    else if (n == QStringLiteral("GuidelineDashPattern")) base.guidelineDashPattern = xml::readFloatArray(r);
    else if (n == QStringLiteral("Unit"))                 base.unit = xml::readIntElement(r);
    else if (n == QStringLiteral("MeasureFont"))          base.measureFont = xml::readFont(r, dataVersion);
    else if (n == QStringLiteral("MeasureFontColor"))     base.measureFontColor = xml::readColor(r);
    else return false;
    return true;
}

void writeRulerBase(QXmlStreamWriter& w, const core::RulerItemBase& b) {
    writeItemCommon(w, b);
    xml::writeColor       (w, QStringLiteral("Color"),                b.color);
    xml::writeFloatElement(w, QStringLiteral("LineThickness"),        b.lineThickness);
    xml::writeBoolElement (w, QStringLiteral("DisplayDistance"),      b.displayDistance);
    xml::writeBoolElement (w, QStringLiteral("DisplayUnit"),          b.displayUnit);
    xml::writeColor       (w, QStringLiteral("GuidelineColor"),       b.guidelineColor);
    xml::writeFloatElement(w, QStringLiteral("GuidelineThickness"),   b.guidelineThickness);
    xml::writeFloatArray  (w, QStringLiteral("GuidelineDashPattern"), b.guidelineDashPattern);
    xml::writeIntElement  (w, QStringLiteral("Unit"),                 b.unit);
    xml::writeFont        (w, QStringLiteral("MeasureFont"),          b.measureFont);
    xml::writeColor       (w, QStringLiteral("MeasureFontColor"),     b.measureFontColor);
}

core::LayerRuler::AnyRuler readRulerItem(QXmlStreamReader& r, int dataVersion) {
    core::LayerRuler::AnyRuler out;
    const bool isLinear = (r.name() == QStringLiteral("LinearRuler"));
    out.kind = isLinear ? core::RulerKind::Linear : core::RulerKind::Circular;
    auto& base = isLinear ? static_cast<core::RulerItemBase&>(out.linear)
                          : static_cast<core::RulerItemBase&>(out.circular);
    // Upstream reads LayerItem common (DisplayArea + MyGroup) then ruler-base
    // fields then subclass fields, all in a fixed order. We loop on start
    // elements and dispatch by name to stay lenient against minor reordering.
    readItemCommon(r, base, dataVersion);
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (consumeRulerBaseField(r, base, dataVersion, n)) continue;
        if (isLinear) {
            if      (n == QStringLiteral("Point1"))         out.linear.point1 = xml::readPointF(r);
            else if (n == QStringLiteral("Point2"))         out.linear.point2 = xml::readPointF(r);
            else if (n == QStringLiteral("AttachedBrick1")) out.linear.attachedBrick1Id = xml::readTextElement(r);
            else if (n == QStringLiteral("AttachedBrick2")) out.linear.attachedBrick2Id = xml::readTextElement(r);
            else if (n == QStringLiteral("OffsetDistance")) out.linear.offsetDistance = xml::readFloatElement(r);
            else if (n == QStringLiteral("AllowOffset"))    out.linear.allowOffset = xml::readBoolElement(r);
            else r.skipCurrentElement();
        } else {
            if      (n == QStringLiteral("Center"))        out.circular.center = xml::readPointF(r);
            else if (n == QStringLiteral("Radius"))        out.circular.radius = xml::readFloatElement(r);
            else if (n == QStringLiteral("AttachedBrick")) out.circular.attachedBrickId = xml::readTextElement(r);
            else r.skipCurrentElement();
        }
    }
    return out;
}

void writeRulerItem(QXmlStreamWriter& w, const core::LayerRuler::AnyRuler& any) {
    // Ruler subtypes (LinearRuler / CircularRuler) do not emit an id attribute
    // upstream — only <Brick> does.
    if (any.kind == core::RulerKind::Linear) {
        w.writeStartElement(QStringLiteral("LinearRuler"));
        writeRulerBase(w, any.linear);
        xml::writePointF      (w, QStringLiteral("Point1"), any.linear.point1);
        xml::writePointF      (w, QStringLiteral("Point2"), any.linear.point2);
        xml::writeTextElement (w, QStringLiteral("AttachedBrick1"), any.linear.attachedBrick1Id);
        xml::writeTextElement (w, QStringLiteral("AttachedBrick2"), any.linear.attachedBrick2Id);
        xml::writeFloatElement(w, QStringLiteral("OffsetDistance"), any.linear.offsetDistance);
        xml::writeBoolElement (w, QStringLiteral("AllowOffset"),    any.linear.allowOffset);
        w.writeEndElement();
    } else {
        w.writeStartElement(QStringLiteral("CircularRuler"));
        writeRulerBase(w, any.circular);
        xml::writePointF      (w, QStringLiteral("Center"), any.circular.center);
        xml::writeFloatElement(w, QStringLiteral("Radius"), any.circular.radius);
        xml::writeTextElement (w, QStringLiteral("AttachedBrick"), any.circular.attachedBrickId);
        w.writeEndElement();
    }
}

std::unique_ptr<core::LayerRuler> readLayerRuler(QXmlStreamReader& r, int dataVersion) {
    auto out = std::make_unique<core::LayerRuler>();
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (isCommonField(n, dataVersion)) { readCommonField(r, *out, dataVersion, n); continue; }
        if (n == QStringLiteral("RulerItems")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("LinearRuler") || r.name() == QStringLiteral("CircularRuler")) {
                    out->rulers.push_back(readRulerItem(r, dataVersion));
                } else r.skipCurrentElement();
            }
        } else if (n == QStringLiteral("Groups")) {
            readGroupsTail(r, out->groups);
        } else r.skipCurrentElement();
    }
    return out;
}

void writeLayerRuler(QXmlStreamWriter& w, const core::LayerRuler& r) {
    writeLayerCommonHeader(w, r, QStringLiteral("ruler"));
    w.writeStartElement(QStringLiteral("RulerItems"));
    for (const auto& item : r.rulers) writeRulerItem(w, item);
    w.writeEndElement();
    writeGroupsTail(w, r.groups);
    w.writeEndElement();
}

}

LayerReadOutcome readLayer(QXmlStreamReader& r, int dataVersion) {
    const QXmlStreamAttributes attrs = r.attributes();
    const QString type = attrs.value(QStringLiteral("type")).toString();
    const QString guid = attrs.value(QStringLiteral("id")).toString();

    LayerReadOutcome out;
    if      (type == QStringLiteral("grid"))  { auto l = readLayerGrid(r, dataVersion);  l->guid = guid; out.layer = std::move(l); }
    else if (type == QStringLiteral("brick")) { auto l = readLayerBrick(r, dataVersion); l->guid = guid; out.layer = std::move(l); }
    else if (type == QStringLiteral("text"))  { auto l = readLayerText(r, dataVersion);  l->guid = guid; out.layer = std::move(l); }
    else if (type == QStringLiteral("area"))  { auto l = readLayerArea(r, dataVersion);  l->guid = guid; out.layer = std::move(l); }
    else if (type == QStringLiteral("ruler")) { auto l = readLayerRuler(r, dataVersion); l->guid = guid; out.layer = std::move(l); }
    else {
        r.skipCurrentElement();
        out.warning = QStringLiteral("Unknown sheet type '%1'; skipped.").arg(type);
    }
    return out;
}

void writeLayer(QXmlStreamWriter& w, const core::Layer& layer) {
    switch (layer.kind()) {
        case core::LayerKind::Grid:  writeLayerGrid (w, static_cast<const core::LayerGrid&>(layer));  break;
        case core::LayerKind::Brick: writeLayerBrick(w, static_cast<const core::LayerBrick&>(layer)); break;
        case core::LayerKind::Text:  writeLayerText (w, static_cast<const core::LayerText&>(layer));  break;
        case core::LayerKind::Area:  writeLayerArea (w, static_cast<const core::LayerArea&>(layer));  break;
        case core::LayerKind::Ruler: writeLayerRuler(w, static_cast<const core::LayerRuler&>(layer)); break;
        case core::LayerKind::AnchoredText:
            // Fork-only; serialized in .bbm.bld sidecar, not in the vanilla .bbm.
            break;
    }
}

}
