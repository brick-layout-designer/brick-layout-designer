// A placed module and its Module library copy (edit/ModuleLibraryLink.h has the
// rules; the web's moduleLibrary.ts does the same):
//   - Make a module: the picked parts become a module in this layout only;
//     "Also save to my Module library" saves it there too.
//   - Save to Module library…: to you or a club on the signed-in server (or the
//     Module library folder on this computer), then linked.
//   - Update Module library version: the module's parts become the Module library's next
//     version (or are written over its library file).
//   - Update from Module library: a newer version comes in where the module sits,
//     each part on its sheet; it asks first when the module was changed here.

#include "MainWindow.h"

#include "ConfirmDialog.h"
#include "MapView.h"
#include "ModuleLibraryDialogs.h"
#include "ModuleLibraryMenu.h"
#include "ModulesPanel.h"
#include "SaveModuleDialog.h"
#include "ServerLibrary.h"
#include "SheetChoiceDialog.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/ModuleEdit.h"
#include "../edit/ModuleCommands.h"
#include "../edit/ModuleLibraryLink.h"
#include "../edit/ModuleSheets.h"
#include "../parts/BrickPlacement.h"
#include "../saveload/BbmReader.h"
#include "../saveload/BbmWriter.h"
#include "ModuleDoc.h"

#include <QDateTime>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QStatusBar>
#include <QUndoStack>

namespace bld::ui {

namespace {

// A module's parts as a library module: one parts sheet per layout sheet
// they are on (same name, fade, visibility), in sheet order.
std::optional<core::Map> moduleMapOf(const core::Map& map, const core::Module& mod) {
    core::Map out;
    out.author = map.author;
    out.lug = map.lug;
    out.event = mod.name.isEmpty() ? QStringLiteral("Module") : mod.name;
    int total = 0;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        auto outL = std::make_unique<core::LayerBrick>();
        outL->guid = core::newBbmId();
        outL->name = L->name.isEmpty() ? QStringLiteral("Module") : L->name;
        outL->transparency = L->transparency;
        outL->visible = L->visible;
        outL->hull = L->hull;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (mod.memberIds.contains(b.guid)) outL->bricks.push_back(b);
        if (outL->bricks.empty()) continue;
        total += static_cast<int>(outL->bricks.size());
        out.layers().push_back(std::move(outL));
    }
    if (total == 0) return std::nullopt;
    out.nbItems = total;
    return out;
}

// Centred on the origin, as the web saves modules, so both apps place it alike.
void centre(core::Map& module) {
    QRectF box;
    for (const auto& layer : module.layers())
        if (layer && layer->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
                box = box.isNull() ? b.displayArea : box.united(b.displayArea);
    const QPointF shift = -box.center();
    for (auto& layer : module.layers())
        if (layer && layer->kind() == core::LayerKind::Brick)
            for (auto& b : static_cast<core::LayerBrick&>(*layer).bricks) b.displayArea.translate(shift);
}

// A module linked to a .bbm file (saved to this computer, or imported from one).
QString libraryFileOf(const core::Module& m) {
    if (m.sourceFile.isEmpty() || m.sourceFile.contains(QLatin1String("://"))) return {};
    if (!m.sourceFile.endsWith(QLatin1String(".bbm"), Qt::CaseInsensitive)) return {};
    return QFileInfo::exists(m.sourceFile) ? m.sourceFile : QString();
}

}  // namespace

void MainWindow::onMakeModule() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    std::vector<edit::CreateModuleCommand::Member> members;
    for (QGraphicsItem* it : mapView_->scene()->selectedItems()) {
        if (it->data(2).toString() != QStringLiteral("brick")) continue;
        members.push_back({ it->data(0).toInt(), it->data(1).toString() });
    }
    if (members.empty()) {
        QMessageBox::information(this, tr("Make a module"), tr("Select one or more parts first, then choose Make a module."));
        return;
    }
    MakeModuleDialog dialog(serverLibrary_, QString(), static_cast<int>(members.size()), this);
    {
        core::Module picked;
        for (const auto& m : members) picked.memberIds.insert(m.guid);
        if (auto module = moduleMapOf(*map, picked)) dialog.setSheets(edit::moduleSheetNames(*module), edit::oneSheetName(*module));
    }
    if (dialog.exec() != QDialog::Accepted) return;
    const MakeModuleDialog::Choice c = dialog.choice();
    auto* cmd = new edit::CreateModuleCommand(*map, c.name, std::move(members));
    const QString id = cmd->moduleId();
    mapView_->undoStack()->push(cmd);
    modulesPanel_->setMap(map);
    if (!c.alsoSave) {
        statusBar()->showMessage(tr("“%1” is a module in this layout. To use it in other layouts, choose Save to "
                                    "Module library… from its menu.")
                                     .arg(c.name),
                                 8000);
        return;
    }
    const core::Module* made = core::findModule(map->sidecar.modules, id);
    auto module = made ? moduleMapOf(*map, *made) : std::nullopt;
    if (!module) return;
    if (c.oneSheet) edit::putOnOneSheet(*module);
    saveModuleMap(std::move(*module), id, c.name, c.onThisComputer, c.orgSlug, QString(), QString());
}

void MainWindow::saveModuleToLibrary(const QString& moduleId) {
    auto* map = mapView_->currentMap();
    const core::Module* mod = map ? core::findModule(map->sidecar.modules, moduleId) : nullptr;
    if (!mod) return;
    auto module = moduleMapOf(*map, *mod);
    if (!module) {
        QMessageBox::information(this, tr("Save to Module library"), tr("This module has no parts to save."));
        return;
    }
    const QString name = mod->name;
    // Signed in to a server: you, a club, or this computer.
    if (serverLibrary_ && serverLibrary_->state() == ServerLibrary::State::Ready) {
        SaveModuleDialog dialog(*serverLibrary_, QString(), this);
        dialog.setModuleName(name);
        dialog.setSheets(edit::moduleSheetNames(*module), edit::oneSheetName(*module));
        if (dialog.exec() != QDialog::Accepted) return;
        const SaveModuleDialog::Choice c = dialog.choice();
        if (c.oneSheet) edit::putOnOneSheet(*module);
        saveModuleMap(std::move(*module), moduleId, c.title, c.onThisComputer, c.orgSlug, c.updateId, c.note);
        return;
    }
    if (!SheetChoiceDialog::askModuleSheets(this, *module)) return;
    saveModuleMap(std::move(*module), moduleId, name, true, QString(), QString(), QString());
}

void MainWindow::saveModuleMap(core::Map module, const QString& moduleId, const QString& title, bool onThisComputer,
                               const QString& orgSlug, const QString& updateId, const QString& note) {
    const int partCount = sync::partCount(module);
    if (onThisComputer) {
        const QString path = saveModuleLocally(module, partCount, title);
        if (!path.isEmpty()) linkModule(moduleId, QString(), 0, path);
        return;
    }
    centre(module);
    uploadModule(serverLibrary_->api(), std::make_shared<core::Map>(std::move(module)), updateId, title, orgSlug, note,
                 [this, moduleId](bool saved, const QString& id, int version) {
                     if (saved) linkModule(moduleId, id, version, serverLibrary_->api().moduleWebUrl(id).toString());
                 });
}

void MainWindow::linkModule(const QString& moduleId, const QString& libraryId, int version, const QString& sourceFile) {
    auto* map = mapView_->currentMap();
    const core::Module* mod = map ? core::findModule(map->sidecar.modules, moduleId) : nullptr;
    if (!mod) return;
    core::Module linked = *mod;
    if (!libraryId.isEmpty()) {
        linked.libraryModuleId = libraryId;
        linked.libraryVersion = std::max(0, version);
    }
    if (!sourceFile.isEmpty()) {
        linked.sourceFile = sourceFile;
        linked.importedAt = QDateTime::currentDateTimeUtc();
    }
    mapView_->undoStack()->push(new edit::UpdateModuleCommand(*map, linked, tr("Link module to the Module library")));
    modulesPanel_->setMap(map);
}

ModuleLibraryInfo MainWindow::moduleLibraryInfo(const core::Module& m) const {
    ModuleLibraryInfo info;
    const ModuleLibraryEntry save{ QStringLiteral("save"), tr("Save to Module library..."), true,
                                   tr("Save a copy to your Module library or a club's, to use in other layouts") };
    if (const auto link = edit::libraryLink(m)) {
        const bool ready = serverLibrary_ && serverLibrary_->state() == ServerLibrary::State::Ready;
        const sync::ServerModule* sm = ready ? serverLibrary_->module(link->id) : nullptr;
        if (!sm) {
            info.note = edit::libraryNote(link->version, -1);
            info.entries << save;
            return info;
        }
        const int latest = sm->latestVersion;
        info.note = edit::libraryNote(link->version, latest);
        info.entries << ModuleLibraryEntry{ QStringLiteral("publish"), tr("Update Module library version..."), sm->canEdit(),
                                            sm->canEdit() ? tr("Make this layout's copy version %1 of “%2”").arg(latest + 1).arg(sm->title)
                                                          : tr("You can't change “%1” in the Module library").arg(sm->title) };
        if (latest > 0 && (link->version == 0 || latest > link->version))
            info.entries << ModuleLibraryEntry{ QStringLiteral("pull"), tr("Update from Module library (v%1)").arg(latest), true,
                                                tr("Replace this module's parts with version %1 from the Module library").arg(latest) };
        if (!sm->canEdit()) info.entries << save;
        return info;
    }
    if (const QString file = libraryFileOf(m); !file.isEmpty()) {
        info.note = tr("from %1").arg(QFileInfo(file).fileName());
        info.entries << ModuleLibraryEntry{ QStringLiteral("publish"), tr("Update Module library file..."), true,
                                            tr("Write this layout's copy over %1").arg(QFileInfo(file).fileName()) }
                     << ModuleLibraryEntry{ QStringLiteral("pull"), tr("Update from Module library file"), true,
                                            tr("Replace this module's parts with what %1 holds now").arg(QFileInfo(file).fileName()) }
                     << save;
        return info;
    }
    info.entries << save;
    return info;
}

void MainWindow::onModuleLibraryAction(const QString& moduleId, const QString& action) {
    if (action == QLatin1String("save")) saveModuleToLibrary(moduleId);
    else if (action == QLatin1String("publish")) publishModule(moduleId);
    else if (action == QLatin1String("pull")) pullModule(moduleId);
}

void MainWindow::publishModule(const QString& moduleId) {
    auto* map = mapView_->currentMap();
    const core::Module* mod = map ? core::findModule(map->sidecar.modules, moduleId) : nullptr;
    if (!mod) return;
    auto module = moduleMapOf(*map, *mod);
    if (!module) return;
    if (const auto link = edit::libraryLink(*mod)) {
        const sync::ServerModule* sm = serverLibrary_ ? serverLibrary_->module(link->id) : nullptr;
        if (!sm) return;
        PublishModuleDialog dialog(sm->title, link->version, sm->latestVersion, sm->canEdit(), this);
        dialog.setSheets(edit::moduleSheetNames(*module), edit::oneSheetName(*module));
        if (dialog.exec() != QDialog::Accepted) return;
        if (dialog.oneSheet()) edit::putOnOneSheet(*module);
        saveModuleMap(std::move(*module), moduleId, sm->title, false, QString(), sm->id, dialog.note());
        return;
    }
    const QString file = libraryFileOf(*mod);
    if (file.isEmpty()) return;
    ConfirmOptions o;
    o.title = tr("Write over %1?").arg(QFileInfo(file).fileName());
    o.removes = tr("What the file holds now is replaced by this module's parts in this layout.");
    o.keeps = tr("Layouts that use it can then update to it.");
    o.confirmLabel = tr("Save");
    o.danger = false;
    if (!confirmModuleLibrary(o)) return;
    const auto r = saveload::writeBbm(*module, file);
    if (!r.ok) {
        QMessageBox::warning(this, tr("Update Module library file"), r.error);
        return;
    }
    linkModule(moduleId, QString(), 0, file);
    statusBar()->showMessage(tr("Saved “%1” over %2").arg(mod->name, QFileInfo(file).fileName()), 5000);
}

bool MainWindow::confirmModuleLibrary(const ConfirmOptions& o) {
    return confirmModuleLibrary_ ? confirmModuleLibrary_(o) : ConfirmDialog::ask(this, o);
}

void MainWindow::pullModule(const QString& moduleId) {
    auto* map = mapView_->currentMap();
    const core::Module* mod = map ? core::findModule(map->sidecar.modules, moduleId) : nullptr;
    if (!mod) return;
    const QString name = mod->name.isEmpty() ? tr("this module") : mod->name;
    const auto ask = [this, name](int version) {
        ConfirmOptions o;
        o.title = version > 0 ? tr("Replace “%1” with version %2?").arg(name).arg(version)
                              : tr("Replace “%1” with the Module library file's parts?").arg(name);
        o.removes = tr("This module was changed in this layout. Those changes are replaced by the Module library's version.");
        o.keeps = tr("Its name, colours and place on the map stay. You can undo this.");
        o.confirmLabel = tr("Update");
        o.danger = false;
        return confirmModuleLibrary(o);
    };
    if (const auto link = edit::libraryLink(*mod)) {
        const sync::ServerModule* sm = serverLibrary_ ? serverLibrary_->module(link->id) : nullptr;
        if (!sm) return;
        const int latest = sm->latestVersion;
        const QString libraryId = link->id;
        const int was = link->version;
        auto failed = [this](const sync::ServerRefusal& r) {
            QMessageBox::warning(this, tr("Update from Module library"),
                                 tr("Couldn't update from the Module library: %1").arg(ServerLibrary::refusalText(r)));
        };
        serverLibrary_->api().moduleSnapshot(libraryId, [this, moduleId, libraryId, latest, was, ask, failed](const QByteArray& bytes) {
            QString error;
            std::shared_ptr<core::Map> lib(sync::mapFromModuleSnapshot(bytes, &error).release());
            if (!lib) {
                QMessageBox::warning(this, tr("Update from Module library"), tr("The module from the Module library couldn't be read (%1).").arg(error));
                return;
            }
            auto apply = [this, moduleId, libraryId, latest, lib] { replaceWithLibrary(moduleId, *lib, libraryId, latest); };
            if (was <= 0) {
                if (ask(latest)) apply();
                return;
            }
            // Changed in this layout? Compare with the version it was linked at.
            serverLibrary_->api().moduleVersionSnapshot(libraryId, was, [this, moduleId, latest, ask, apply](const QByteArray& old) {
                std::unique_ptr<core::Map> then = sync::mapFromModuleSnapshot(old);
                auto* map = mapView_->currentMap();
                const core::Module* m = map ? core::findModule(map->sidecar.modules, moduleId) : nullptr;
                if (!m) return;
                bool changed = true;
                if (then) {
                    parts::placement::fixStaleAreas(*then, parts_);
                    changed = !edit::matchesVersion(edit::batchesOfModule(*then), edit::placedParts(*map, m->memberIds), parts_);
                }
                if (!changed || ask(latest)) apply();
            }, [ask, apply, latest](const sync::ServerRefusal&) {
                if (ask(latest)) apply();
            });
        }, failed);
        return;
    }
    const QString file = libraryFileOf(*mod);
    if (file.isEmpty()) return;
    auto res = saveload::readBbm(file);
    if (!res.ok()) {
        QMessageBox::warning(this, tr("Update from Module library file"), tr("Could not read %1: %2").arg(file, res.error));
        return;
    }
    parts::placement::fixStaleAreas(*res.map, parts_);
    if (edit::matchesVersion(edit::batchesOfModule(*res.map), edit::placedParts(*map, mod->memberIds), parts_)) {
        statusBar()->showMessage(tr("“%1” already matches %2").arg(name, QFileInfo(file).fileName()), 5000);
        return;
    }
    if (!ask(0)) return;
    replaceWithLibrary(moduleId, *res.map, mod->libraryModuleId, mod->libraryVersion);
}

void MainWindow::replaceWithLibrary(const QString& moduleId, core::Map& library, const QString& libraryId, int version) {
    auto* map = mapView_->currentMap();
    const core::Module* mod = map ? core::findModule(map->sidecar.modules, moduleId) : nullptr;
    if (!mod) return;
    parts::placement::fixStaleAreas(library, parts_);
    auto batches = edit::batchesOfModule(library);
    if (batches.empty()) {
        QMessageBox::information(this, tr("Update from Module library"), tr("The Module library's version has no parts."));
        return;
    }
    const auto placed = edit::placedParts(*map, mod->memberIds);
    const edit::Placement placement = edit::alignToPlaced(batches, placed, parts_);
    batches = edit::placeVersion(std::move(batches), placement, parts_);
    const auto sheetFor = edit::sheetForUpdate(*map, mod->memberIds);
    for (auto& batch : batches) batch.targetLayerGuid = sheetFor(batch.layerName);
    const QString name = mod->name;
    mapView_->undoStack()->push(new edit::ReplaceModulePartsCommand(*map, moduleId, std::move(batches), libraryId, version));
    mapView_->rebuildScene();
    modulesPanel_->setMap(map);
    statusBar()->showMessage(tr("Updated “%1” from the Module library").arg(name), 5000);
}

}  // namespace bld::ui
