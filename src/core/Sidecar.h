#pragma once

#include "AnchoredLabel.h"
#include "Module.h"
#include "SavedView.h"
#include "Venue.h"

#include <QByteArray>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

namespace bld::core {

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

    bool isEmpty() const {
        return anchoredLabels.empty() && modules.empty() && !venue.has_value()
            && backgroundImagePath.isEmpty() && views.empty();
    }
};

}
