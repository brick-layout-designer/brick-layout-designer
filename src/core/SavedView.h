#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <optional>

namespace bld::core {

// A saved view (the web's references/LAYOUT-FILE.md "Saved views"): a
// named picture of the layout that can be shown again, shared or
// exported. Everything is in studs. Kept in the sidecar under `views`, so
// it travels in .bld-layout files and syncs live through meta.cache.
struct SavedView {
    QString id;    // unique in the layout; a view without one is dropped on reading
    QString name;  // shown in the list and used in picture file names
    // true: fit the whole layout, worked out each time a picture is made
    // (rect is then ignored and written as null). false: use rect.
    bool fit = true;
    std::optional<QRectF> rect;  // x, y = top left; width, height
    // Layer ids shown, or unset to follow the layout's own sheet on/off.
    // The grid layer is not a sheet here.
    std::optional<QStringList> sheets;
    bool grid = true;    // draw the grid layer's lines under the picture
    bool labels = true;  // show the anchored labels
    // Fields this build doesn't know, kept as they were.
    QJsonObject extras{};

    bool operator==(const SavedView& o) const {
        return id == o.id && name == o.name && fit == o.fit && rect == o.rect && sheets == o.sheets
            && grid == o.grid && labels == o.labels && extras == o.extras;
    }
    bool operator!=(const SavedView& o) const { return !(*this == o); }
};

}  // namespace bld::core
