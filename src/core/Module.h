#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTransform>

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
    bool    sameColor = true;  // "Same colour": outline and name change together

    // Fields a newer build (or the web) wrote that this one doesn't know,
    // kept so reading and writing the module again doesn't lose them.
    QJsonObject extras;
};

}
