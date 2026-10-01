#include "BbmReader.h"

#include "LayerIO.h"
#include "XmlPrimitives.h"

#include "../core/Brick.h"
#include "../core/Group.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"

#include <QFile>
#include <QHash>
#include <QXmlStreamReader>

#include <vector>

namespace bld::saveload {

namespace {

void readDateElement(QXmlStreamReader& r, QDate& date) {
    int d = 1, m = 1, y = 2000;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("Day"))   d = xml::readIntElement(r);
        else if (n == QStringLiteral("Month")) m = xml::readIntElement(r);
        else if (n == QStringLiteral("Year"))  y = xml::readIntElement(r);
        else r.skipCurrentElement();
    }
    const QDate parsed(y, m, d);
    date = parsed.isValid() ? parsed : QDate(2000, 1, 1);
}

void readExportInfo(QXmlStreamReader& r, core::ExportInfo& info, int dataVersion) {
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("ExportPath"))           info.exportPath = xml::readTextElement(r);
        else if (n == QStringLiteral("ExportFileType"))       info.fileTypeIndex = xml::readIntElement(r);
        else if (n == QStringLiteral("ExportArea"))           info.area = xml::readRectF(r);
        else if (n == QStringLiteral("ExportScale"))          info.scale = xml::readDoubleElement(r);
        else if (n == QStringLiteral("ExportWatermark"))      info.watermark = xml::readBoolElement(r);
        else if (n == QStringLiteral("ExportBrickHull"))      (void)xml::readBoolElement(r); // v8 only; dropped on write in v9
        else if (n == QStringLiteral("ExportElectricCircuit")) info.electricCircuit = xml::readBoolElement(r);
        else if (n == QStringLiteral("ExportConnectionPoints")) info.connectionPoints = xml::readBoolElement(r);
        else r.skipCurrentElement();
    }
    (void)dataVersion;
}

QString readLayersInto(QXmlStreamReader& r, core::Map& map, int dataVersion) {
    int skipped = 0;
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("Layer")) {
            r.skipCurrentElement();
            continue;
        }
        auto outcome = readLayer(r, dataVersion);
        if (outcome.layer) {
            map.layers().push_back(std::move(outcome.layer));
        } else {
            ++skipped;
        }
    }
    if (skipped == 0) return {};
    return QStringLiteral("%1 layer(s) skipped (unsupported type)").arg(skipped);
}

LoadResult readMapElement(QXmlStreamReader& r) {
    auto map = std::make_unique<core::Map>();
    QString warning;

    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (n == QStringLiteral("Version")) {
            map->dataVersion = xml::readIntElement(r);
            if (map->dataVersion > core::Map::kCurrentDataVersion) {
                return { nullptr, QStringLiteral(
                    "File data version %1 is newer than supported (%2).")
                        .arg(map->dataVersion).arg(core::Map::kCurrentDataVersion) };
            }
        } else if (n == QStringLiteral("nbItems")) {
            map->nbItems = xml::readIntElement(r);
        } else if (n == QStringLiteral("BackgroundColor")) {
            map->backgroundColor = xml::readColor(r);
        } else if (n == QStringLiteral("Author")) {
            map->author = xml::readTextElement(r);
        } else if (n == QStringLiteral("LUG")) {
            map->lug = xml::readTextElement(r);
        } else if (n == QStringLiteral("Event")) {
            map->event = xml::readTextElement(r);
        } else if (n == QStringLiteral("Date")) {
            readDateElement(r, map->date);
        } else if (n == QStringLiteral("Comment")) {
            map->comment = xml::readTextElement(r);
        } else if (n == QStringLiteral("ExportInfo")) {
            readExportInfo(r, map->exportInfo, map->dataVersion);
        } else if (n == QStringLiteral("SelectedLayerIndex")) {
            map->selectedLayerIndex = xml::readIntElement(r);
        } else if (n == QStringLiteral("Layers")) {
            const auto w = readLayersInto(r, *map, map->dataVersion);
            if (!w.isEmpty()) warning = w;
        } else {
            r.skipCurrentElement();
        }
    }

    LoadResult out;
    out.map = std::move(map);
    out.error = warning; // non-fatal warning if layers were skipped
    return out;
}

}

namespace {

// Vanilla BlueBrick requires every id to parse as a decimal ulong. Earlier
// drafts of our fork used QUuid strings for newly-minted ids, which vanilla
// refuses to load. This pass walks every id in the freshly-loaded map and
// remaps anything non-numeric to a fresh numeric id via core::newBbmId(),
// keeping cross-references (MyGroup, connection links) internally consistent
// so the next save produces a vanilla-compatible .bbm. Re-reading a
// vanilla-clean file is a no-op (all ids already numeric).
bool isNumericId(const QString& s) {
    if (s.isEmpty()) return true;              // empty means "no group"
    for (QChar c : s) if (!c.isDigit()) return false;
    return true;
}

QHash<QString, QString> migrateNonNumericIds(core::Map& map) {
    QHash<QString, QString> remap;  // old -> new

    // Item/layer identity. Empty is NOT a valid item id — older .bbm
    // files (and some we generate by hand) skipped the id attribute on
    // rulers and text cells, which collapsed every such item's guid to
    // "". Downstream code keys on guid (scene item tagging, module
    // memberIds, undo commands), so a fresh id is minted for any empty
    // guid here.
    auto ensureId = [&](QString& id) {
        if (!id.isEmpty() && isNumericId(id)) return;
        if (id.isEmpty()) { id = core::newBbmId(); return; }
        auto it = remap.find(id);
        if (it == remap.end()) it = remap.insert(id, core::newBbmId());
        id = it.value();
    };
    // Optional reference: empty means "no affiliation" (e.g. myGroupId
    // on an unaffiliated brick, LinkedTo on a free connection point).
    // Only remap non-empty non-numeric values.
    auto ensureRef = [&](QString& id) {
        if (isNumericId(id)) return;
        auto it = remap.find(id);
        if (it == remap.end()) it = remap.insert(id, core::newBbmId());
        id = it.value();
    };

    for (auto& layerPtr : map.layers()) {
        if (!layerPtr) continue;
        ensureId(layerPtr->guid);

        if (layerPtr->kind() == core::LayerKind::Brick) {
            auto& L = static_cast<core::LayerBrick&>(*layerPtr);
            for (auto& b : L.bricks) {
                ensureId(b.guid);
                ensureRef(b.myGroupId);
                for (auto& cp : b.connections) {
                    ensureId(cp.guid);
                    ensureRef(cp.linkedToId);
                }
            }
            for (auto& g : L.groups) {
                ensureId(g.guid);
                ensureRef(g.myGroupId);
            }
        } else if (layerPtr->kind() == core::LayerKind::Text) {
            auto& L = static_cast<core::LayerText&>(*layerPtr);
            for (auto& c : L.textCells) {
                ensureId(c.guid);
                ensureRef(c.myGroupId);
            }
            for (auto& g : L.groups) {
                ensureId(g.guid);
                ensureRef(g.myGroupId);
            }
        } else if (layerPtr->kind() == core::LayerKind::Ruler) {
            auto& L = static_cast<core::LayerRuler&>(*layerPtr);
            for (auto& any : L.rulers) {
                auto& base = (any.kind == core::RulerKind::Linear)
                                 ? static_cast<core::RulerItemBase&>(any.linear)
                                 : static_cast<core::RulerItemBase&>(any.circular);
                ensureId(base.guid);
                ensureRef(base.myGroupId);
                if (any.kind == core::RulerKind::Linear) {
                    ensureRef(any.linear.attachedBrick1Id);
                    ensureRef(any.linear.attachedBrick2Id);
                } else {
                    ensureRef(any.circular.attachedBrickId);
                }
            }
            for (auto& g : L.groups) {
                ensureId(g.guid);
                ensureRef(g.myGroupId);
            }
        }
    }
    return remap;
}

}

LoadResult readBbm(QIODevice& input) {
    QXmlStreamReader r(&input);
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("Map")) {
            LoadResult result = readMapElement(r);
            if (result.map) result.renamedIds = migrateNonNumericIds(*result.map);
            return result;
        }
        r.skipCurrentElement();
    }
    return { nullptr, QStringLiteral("No <Map> root element found.") };
}

LoadResult readBbm(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return { nullptr, QStringLiteral("Cannot open file: %1").arg(file.errorString()) };
    }
    return readBbm(file);
}

}
