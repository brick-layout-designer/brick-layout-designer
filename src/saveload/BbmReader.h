#pragma once

#include <QHash>
#include <QString>

#include <memory>

namespace bld::core { class Map; }

class QIODevice;

namespace bld::saveload {

struct LoadResult {
    std::unique_ptr<core::Map> map;
    QString error; // non-empty may be a non-fatal warning when map != nullptr
    // Ids that weren't BlueBrick's plain numbers, renumbered on reading
    // (old -> new). The sidecar read beside the map follows them
    // (saveload::renameSidecarIds), so its views, labels and modules still
    // point at the right sheets and bricks.
    QHash<QString, QString> renamedIds;
    bool ok() const { return map != nullptr; }
};

// Load a vanilla BlueBrick .bbm file into core::Map. Phase 1 supports the map
// header (version, author/LUG/event/date/comment, background color, export
// info, selected layer index). Layers are recognized and skipped with a
// non-fatal warning; full layer dispatch arrives in subsequent commits.
LoadResult readBbm(const QString& path);
LoadResult readBbm(QIODevice& input);

}
