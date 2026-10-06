#pragma once

// A placed module and its copy in the Module library, both ways, as the web does
// it (apps/web/src/editor/moduleLibrary.ts).
//
// Make a module: the picked parts become a module in this layout only.
// Save to Module library…: a copy goes to your modules or a club's, and the placed
// module is linked to it (Module::libraryModuleId / libraryVersion, sidecar
// only). Modules added from the Module library are linked the same way. A linked
// module can then make its parts the Module library's next version (Update
// library version), or take a newer library version in (Update from
// library): where the module sits now, each part on its sheet, asking first
// when it was changed in this layout.

#include "ModuleCommands.h"

#include <QPointF>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace bld::core { class Map; struct Module; struct Brick; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

using LayerBatches = std::vector<ImportBbmAsModuleCommand::LayerBatch>;

// The library module a placed module is linked to; `version` 0 when not known.
struct LibraryLink {
    QString id;
    int version = 0;
};

// The placed module's link: libraryModuleId, else the module's page on a
// server that older builds kept in sourceFile (".../modules/<id>").
std::optional<LibraryLink> libraryLink(const core::Module& m);

// A library module's parts as insert batches (one per parts sheet, fresh
// guids, its sets kept).
LayerBatches batchesOfModule(const core::Map& module);

// How a library version sits in the layout: turned by `degrees` about its
// origin, then moved by `to`. `matched`: how many parts agree (0: a guess
// from the module's middle).
struct Placement {
    double degrees = 0.0;
    QPointF to;
    int matched = 0;
};

// Where a library version's parts go so that its parts the layout already
// has land on them: the turn most part pairs agree on, then the shift.
// Robust to some parts having changed on either side; falls back to the
// placed module's middle, unturned, when no parts are alike.
Placement alignToPlaced(const LayerBatches& version, const std::vector<core::Brick>& placed,
                        parts::PartsLibrary& parts);

// The library version's parts where `placement` puts them.
LayerBatches placeVersion(LayerBatches version, const Placement& placement, parts::PartsLibrary& parts);

// Whether the placed parts are exactly the Module library version's (same parts,
// places and turns, within a hair): false when changed in this layout.
bool matchesVersion(const LayerBatches& version, const std::vector<core::Brick>& placed, parts::PartsLibrary& parts);

// The module's parts, on every sheet.
std::vector<core::Brick> placedParts(const core::Map& map, const QSet<QString>& members);

// The sheet (layer guid) a library sheet's parts go on when the module is
// updated: the layout's sheet with the same name, else the sheet most of
// the module's parts are on now, else the picked sheet.
std::function<QString(const QString& sheetName)> sheetForUpdate(const core::Map& map, const QSet<QString>& members);

// What the Modules panel says under a linked module ("in the Module library v3",
// "in the Module library v3 · v5 is newer"); `latest` < 0: the Module library copy isn't
// on the server you're signed in to.
QString libraryNote(int version, int latest);

}  // namespace bld::edit
