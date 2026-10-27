#pragma once

#include "AnchoredLabel.h"
#include "Module.h"
#include "SavedView.h"
#include "Venue.h"

#include <QByteArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

namespace bld::core {

// A custom set placed in the layout (a named group of the .bbm): which
// version of the set it came from (parts::setVersion), so the layout can say
// "Update available" when the set changes (edit/SetUpdates.h). Vanilla
// BlueBrick never sees it; the web writes the same (`sets`, PlacedSet in
// packages/bbm sidecar.ts).
struct PlacedSet {
    QString groupId; // the set's outermost group
    QString version;
    // One of its parts, and where that part sits in the set (studs,
    // degrees), so the set's place and turn can be found however it was
    // moved; empty `anchorBrick` when not known.
    QString anchorBrick;
    QPointF anchorPos;
    double anchorAngle = 0.0;
    QJsonObject extras; // fields a newer build added, kept as they are
};

// Root container for fork-only metadata, serialized to a .bbm.bld sidecar
// beside the .bbm. Vanilla BlueBrick ignores this file; our writer always
// writes both atomically on save.
struct Sidecar {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;

    // SHA-256 of the sibling .bbm bytes at the time of sidecar write. On load,
    // we compare against the current .bbm; drift (e.g. vanilla edited it in
    // between) is flagged so the UI can offer re-link by proximity.
    QByteArray bbmContentHashSha256;

    std::vector<AnchoredLabel> anchoredLabels;
    std::vector<Module>        modules;
    std::optional<Venue>       venue;

    // Optional raster background image painted underneath everything else.
    // Path is absolute; opacity is 0..1. BLD-only feature (vanilla
    // BlueBrick has no background-image support); read/written via the
    // sidecar so we don't disturb .bbm bytes.
    QString backgroundImagePath;
    QRectF  backgroundImageRectStuds;  // null = stretch to scene bounds
    double  backgroundImageOpacity = 0.5;

    // Saved views, in the order the Views panel shows them.
    std::vector<SavedView> views;

    // Custom sets placed in the layout, with the version each came from.
    std::vector<PlacedSet> sets;

    bool isEmpty() const {
        return anchoredLabels.empty() && modules.empty() && !venue.has_value()
               && backgroundImagePath.isEmpty() && views.empty() && sets.empty();
    }
};

}
