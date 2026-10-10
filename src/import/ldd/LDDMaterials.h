#pragma once

#include <QColor>
#include <QHash>
#include <QString>

namespace bld::import {

// LDD's Materials.xml — every material ID a part can carry, with
// RGB(A) values directly. Format:
//   <Material MatID="1" Red="244" Green="244" Blue="244" Alpha="255"
//             MaterialType="shinyPlastic"/>
//
// The file lives at <LDD-install>/Assets/db/Materials.xml after the
// LIF archive is unpacked (or accessible via LifReader::read against
// db.lif). A copy also ships in the LU client at
// res/brickdb/Materials.xml.
//
// Used by the LDD import pipeline when a part has no LDraw equivalent
// in ldraw.xml — we still need to color the rendered .g geometry,
// and Materials.xml is the only authoritative LDD palette source.
class LDDMaterials {
public:
    LDDMaterials() = default;

    // Parse the file at `path`. Successive loads merge into the
    // existing palette (last-write-wins on conflicts).
    bool loadFromFile(const QString& path);

    // Parse pre-loaded XML bytes (e.g. read out of a .lif via
    // LifReader). Same merge semantics.
    bool loadFromBytes(const QByteArray& bytes);

    // Resolve an LDD material ID to a QColor. Returns an invalid
    // QColor when the ID is unknown so callers can fall back.
    QColor color(int matId) const {
        return colors_.value(matId);
    }

    // True if `matId` was explicitly listed in the loaded palette.
    bool contains(int matId) const { return colors_.contains(matId); }

    int count() const { return colors_.size(); }

private:
    QHash<int, QColor> colors_;
};

}  // namespace bld::import
