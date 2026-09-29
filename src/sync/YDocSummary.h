#pragma once

// Reads a collaborative layout's shared document (the Yjs doc the web server
// holds) through yrs. Sync phase P2 go/no-go: proves the desktop can load a
// real server document and find its layers and bricks. The full layout
// mapping comes in P3.

#include <QByteArray>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

namespace bld::sync {

struct BrickSummary {
    QString id;
    QString partNumber;
    QRectF  displayArea;
    double  orientation = 0.0;
};

struct LayerSummary {
    QString id;
    QString type;   // "grid", "brick", "text", "area", "ruler"
    QString name;
    int     brickCount = 0;
    std::optional<BrickSummary> firstBrick;
};

struct DocSummary {
    int schemaVersion = 0;
    std::vector<LayerSummary> layers;
};

// Apply a Yjs v1 state update (Y.encodeStateAsUpdate) to a fresh doc and
// summarise it: meta.schemaVersion, then every id in doc.layers with its
// layerData entry. nullopt (and *error set) when the update can't be applied.
std::optional<DocSummary> summarizeDoc(const QByteArray& update, QString* error = nullptr);

}  // namespace bld::sync
