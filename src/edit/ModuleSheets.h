#pragma once

// How a module's sheets meet a layout's sheets, as the web does it
// (apps/web/src/editor/moduleSheets.ts).
//
// Saving: the picked parts keep the sheets they sit on, or all go on one
// sheet named after the sheet with the most of them. Inserting: each
// module sheet goes on the layout's parts sheet with the same name (case
// and the spaces around it don't count); a sheet with no match is never
// made quietly: the person is asked "Where should these go?"
// (ui/SheetChoiceDialog) — the picked sheet, or a new sheet by that name.

#include "ModuleCommands.h"

#include <QString>
#include <QSet>
#include <QStringList>

#include <vector>

namespace bld::core { class Map; }

namespace bld::edit {

// Two sheet names are the same sheet when they match ignoring case and the spaces around them.
QString sheetKey(const QString& name);

// The first parts sheet named `name` (see sheetKey); -1 when none.
int findPartsSheet(const core::Map& map, const QString& name);

struct UnmatchedSheet {
    QString name;  // the module sheet's name, as the batch carries it
    int parts = 0;
};

// The module's sheets (with parts) no parts sheet in `map` matches, merged by name, in module order.
std::vector<UnmatchedSheet> unmatchedSheets(const core::Map& map,
                                            const std::vector<ImportBbmAsModuleCommand::LayerBatch>& batches);

// How many sheets (with parts) the module has, merged by name.
int moduleSheetCount(const std::vector<ImportBbmAsModuleCommand::LayerBatch>& batches);

// The sheet new parts go on: the picked sheet (selectedLayerIndex) when it's
// a parts sheet, else the top parts sheet; -1 when there's none.
int pickedPartsSheet(const core::Map& map);

// Sends every batch named `name` (see sheetKey) to the layer with this guid (empty: a new sheet by its name).
void sendSheetTo(std::vector<ImportBbmAsModuleCommand::LayerBatch>& batches, const QString& name,
                 const QString& layerGuid);

// The module's parts sheets with parts, in order (for "This module uses 2 sheets: …").
QStringList moduleSheetNames(const core::Map& module);

// The name "Put everything on one sheet" uses: the parts sheet with the most parts (the first, on a tie).
QString oneSheetName(const core::Map& module);

// "Put everything on one sheet": every parts sheet's parts and sets on the
// sheet with the most parts (the first, on a tie), solid; the others go.
void putOnOneSheet(core::Map& module);

// The parts sheets a placed module's parts are on, in sheet order.
struct ModuleSheetUse {
    int index = -1;
    QString name;
    bool visible = true;
    int parts = 0;  // how many of the module's parts are on it
};
std::vector<ModuleSheetUse> moduleSheetsUsed(const core::Map& map, const QSet<QString>& members);

// What the Modules panel says when some or all of a module's sheets are
// hidden ("2 parts are on a hidden sheet", "hidden: its sheets are
// hidden"); empty when all show.
QString hiddenSheetsNote(const std::vector<ModuleSheetUse>& uses);

// Mends a module the web saved before 2026-10, whose sheets were written
// fully see-through: its Save-module sheets (guid "module-layer-N") at
// transparency 0 are made solid. Returns how many were mended.
int repairModuleSheets(core::Map& module);

}  // namespace bld::edit
