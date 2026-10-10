#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTransform>

#include <utility>
#include <vector>

namespace bld::core {

// Fork-only cross-layer group: a named bundle of items that span multiple
// layers and can be transformed as a unit. Vanilla BlueBrick's per-layer
// Group structure can express part of this, but the cross-layer linkage
// lives in the .bbm.bld sidecar.
//
// `members` holds item guids across any layers — bricks, rulers, texts,
// areas (areas have no guid but can be referenced by (layer, x, y) in a
// future extension).
struct Module {
    QString id;
    QString name;
    QSet<QString> memberIds;
    QTransform transform;                       // local-to-world of the module as a unit
    QString    sourceFile;                      // non-empty if imported from a .bbm
    QDateTime  importedAt;                      // when imported (if sourceFile non-empty)

    // This placed module's own look (sidecar only; written when not the
    // default, as the web writes them, packages/bbm sidecar.ts).
    bool    showName = true;   // its name shows on the map (View > Module Names still applies)
    QString outlineColor;      // "#rrggbb", or empty for the default light blue
    QString nameColor;         // "#rrggbb", or empty for the default light blue
    bool    sameColor = true;  // "Same color": outline and name change together
    // Pinned in place: it can't be moved as a whole (Edit module still works).
    bool    pinned = false;
    // The library module this placed module is linked to (its id on the
    // server) and the version it matches (0: not known), set when it is
    // saved to the library or added from it (the web's libraryModuleId /
    // libraryVersion). Sidecar only: BlueBrick never sees them.
    QString libraryModuleId;
    int     libraryVersion = 0;

    // Fields a newer build (or the web) wrote that this one doesn't know,
    // kept so reading and writing the module again doesn't lose them.
    QJsonObject extras;
};

// A copy of `m` holding `members` (Duplicate or paste of one whole module,
// the Modules panel's Duplicate): "X (copy)", the same look, not linked to
// the Module library, not pinned (it is there to be moved), no source file.
inline Module copyOfModule(const Module& m, QSet<QString> members, QString newId) {
    Module c = m;
    c.id = std::move(newId);
    c.name = m.name.isEmpty() ? QStringLiteral("(copy)") : m.name + QStringLiteral(" (copy)");
    c.memberIds = std::move(members);
    c.transform = QTransform();
    c.sourceFile.clear();
    c.importedAt = QDateTime();
    c.pinned = false;
    c.libraryModuleId.clear();
    c.libraryVersion = 0;
    return c;
}

// The one module `picked` is exactly (all its parts, nothing else), or null.
inline const Module* wholeModule(const std::vector<Module>& modules, const QSet<QString>& picked) {
    if (picked.isEmpty()) return nullptr;
    for (const auto& m : modules)
        if (m.memberIds == picked) return &m;
    return nullptr;
}
}
