#include "ModuleSheets.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QObject>
#include <QRegularExpression>

#include <algorithm>

namespace bld::edit {

QString sheetKey(const QString& name) { return name.trimmed().toCaseFolded(); }

int findPartsSheet(const core::Map& map, const QString& name) {
    const QString key = sheetKey(name);
    for (int i = 0; i < static_cast<int>(map.layers().size()); ++i) {
        const auto* L = map.layers()[i].get();
        if (L && L->kind() == core::LayerKind::Brick && sheetKey(L->name) == key) return i;
    }
    return -1;
}

namespace {
QString batchName(const ImportBbmAsModuleCommand::LayerBatch& b) {
    return b.layerName.isEmpty() ? QStringLiteral("Module") : b.layerName;
}
}  // namespace

std::vector<UnmatchedSheet> unmatchedSheets(const core::Map& map,
                                            const std::vector<ImportBbmAsModuleCommand::LayerBatch>& batches) {
    std::vector<UnmatchedSheet> out;
    for (const auto& b : batches) {
        if (b.bricks.empty()) continue;
        const QString name = batchName(b);
        if (findPartsSheet(map, name) >= 0) continue;
        bool merged = false;
        for (auto& u : out) {
            if (sheetKey(u.name) == sheetKey(name)) {
                u.parts += static_cast<int>(b.bricks.size());
                merged = true;
                break;
            }
        }
        if (!merged) out.push_back({ name, static_cast<int>(b.bricks.size()) });
    }
    return out;
}

int moduleSheetCount(const std::vector<ImportBbmAsModuleCommand::LayerBatch>& batches) {
    QStringList keys;
    for (const auto& b : batches)
        if (!b.bricks.empty() && !keys.contains(sheetKey(batchName(b)))) keys.append(sheetKey(batchName(b)));
    return static_cast<int>(keys.size());
}

int pickedPartsSheet(const core::Map& map) {
    const int n = static_cast<int>(map.layers().size());
    const int sel = map.selectedLayerIndex;
    if (sel >= 0 && sel < n && map.layers()[sel] && map.layers()[sel]->kind() == core::LayerKind::Brick) return sel;
    for (int i = n - 1; i >= 0; --i)
        if (map.layers()[i] && map.layers()[i]->kind() == core::LayerKind::Brick) return i;
    return -1;
}

void sendSheetTo(std::vector<ImportBbmAsModuleCommand::LayerBatch>& batches, const QString& name,
                 const QString& layerGuid) {
    for (auto& b : batches)
        if (sheetKey(batchName(b)) == sheetKey(name)) b.targetLayerGuid = layerGuid;
}

QStringList moduleSheetNames(const core::Map& module) {
    QStringList out;
    for (const auto& L : module.layers())
        if (L && L->kind() == core::LayerKind::Brick && !static_cast<const core::LayerBrick&>(*L).bricks.empty())
            out.append(L->name);
    return out;
}

namespace {
std::vector<core::LayerBrick*> sheetsWithParts(core::Map& module) {
    std::vector<core::LayerBrick*> out;
    for (auto& L : module.layers())
        if (L && L->kind() == core::LayerKind::Brick && !static_cast<core::LayerBrick&>(*L).bricks.empty())
            out.push_back(static_cast<core::LayerBrick*>(L.get()));
    return out;
}
core::LayerBrick* mostParts(const std::vector<core::LayerBrick*>& sheets) {
    core::LayerBrick* best = nullptr;
    for (auto* s : sheets)
        if (!best || s->bricks.size() > best->bricks.size()) best = s;
    return best;
}
}  // namespace

QString oneSheetName(const core::Map& module) {
    const core::LayerBrick* best = nullptr;
    for (const auto& L : module.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& B = static_cast<const core::LayerBrick&>(*L);
        if (!B.bricks.empty() && (!best || B.bricks.size() > best->bricks.size())) best = &B;
    }
    return best ? best->name : QString();
}

void putOnOneSheet(core::Map& module) {
    const auto sheets = sheetsWithParts(module);
    if (sheets.size() < 2) return;
    core::LayerBrick* keep = mostParts(sheets);
    for (auto* s : sheets) {
        if (s == keep) continue;
        for (auto& b : s->bricks) keep->bricks.push_back(std::move(b));
        for (auto& g : s->groups) keep->groups.push_back(std::move(g));
        s->bricks.clear();
        s->groups.clear();
    }
    keep->transparency = 100;
    keep->visible = true;
    auto& layers = module.layers();
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                                [keep](const auto& L) {
                                    return L && L->kind() == core::LayerKind::Brick && L.get() != keep
                                           && static_cast<const core::LayerBrick&>(*L).bricks.empty();
                                }),
                 layers.end());
}

std::vector<ModuleSheetUse> moduleSheetsUsed(const core::Map& map, const QSet<QString>& members) {
    std::vector<ModuleSheetUse> out;
    if (members.isEmpty()) return out;
    for (int i = 0; i < static_cast<int>(map.layers().size()); ++i) {
        const auto* L = map.layers()[i].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        int n = 0;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (members.contains(b.guid)) ++n;
        if (n > 0) out.push_back({ i, L->name, L->visible, n });
    }
    return out;
}

QString hiddenSheetsNote(const std::vector<ModuleSheetUse>& uses) {
    int hidden = 0, parts = 0;
    for (const auto& u : uses)
        if (!u.visible) {
            ++hidden;
            parts += u.parts;
        }
    if (hidden == 0) return {};
    if (hidden == static_cast<int>(uses.size()))
        return uses.size() == 1 ? QObject::tr("hidden: its sheet is hidden") : QObject::tr("hidden: its sheets are hidden");
    return parts == 1 ? QObject::tr("1 part is on a hidden sheet") : QObject::tr("%1 parts are on a hidden sheet").arg(parts);
}

int repairModuleSheets(core::Map& module) {
    static const QRegularExpression saved(QStringLiteral("^module-layer-\\d+$"));
    int mended = 0;
    for (auto& L : module.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick || L->transparency != 0) continue;
        if (!saved.match(L->guid).hasMatch()) continue;
        L->transparency = 100;
        ++mended;
    }
    return mended;
}

}  // namespace bld::edit
