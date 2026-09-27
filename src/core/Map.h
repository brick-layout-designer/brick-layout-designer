#pragma once

#include "ColorSpec.h"
#include "Ids.h"
#include "Sidecar.h"

#include <QDate>
#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

namespace bld::core {

class Layer;

// Mirrors the image-export metadata block in vanilla BlueBrick's <ExportInfo>.
struct ExportInfo {
    QString exportPath;          // saved relative to .bbm file; absolute path resolved after load
    int     fileTypeIndex = 1;
    QRectF  area;
    double  scale = 0.0;
    bool    watermark = false;
    bool    electricCircuit = false;
    bool    connectionPoints = false;
};

class Map {
public:
    Map();
    ~Map();
    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;
    Map(Map&&) noexcept;
    Map& operator=(Map&&) noexcept;

    // Current data version that Phase 1 reads/writes. Matches CURRENT_DATA_VERSION
    // in upstream MapData/Map.cs (v9 as of BlueBrick 1.9.2.0).
    static constexpr int kCurrentDataVersion = 9;

    // ---------- header ----------

    int     dataVersion = kCurrentDataVersion;

    // Count of content items across all layers. Vanilla computes this at save
    // time from each layer's NbItems. We preserve the value read from disk so
    // load-save round-trips match byte-for-byte even before Phase 2 rendering
    // code starts mutating layer contents. When mutations land, the writer
    // should recompute via LayerBrick::bricks.size() etc.
    int     nbItems = 0;

    QString author;
    QString lug;
    QString event;
    QDate   date = QDate::currentDate();
    QString comment;

    ColorSpec backgroundColor = ColorSpec::fromKnown(QColor(Qt::white), QStringLiteral("White"));

    ExportInfo exportInfo;

    // TrackDesigner (.tdl) document options. Like BlueBrick, kept in memory
    // only (not part of .bbm) so a .tdl opened and saved keeps them.
    struct TrackDesignerOptions {
        bool allowElectricShortCuts = false;
        bool allowUnderground = false;
        bool allowSteps = false;
        bool allowSlopeMismatch = false;
    } trackDesigner;

    int     selectedLayerIndex = -1;

    // ---------- content ----------

    const std::vector<std::unique_ptr<Layer>>& layers() const { return layers_; }
    std::vector<std::unique_ptr<Layer>>&       layers()       { return layers_; }

    // Fork-only metadata (anchored labels, modules, venue). Lives alongside the
    // .bbm in a .bbm.bld sidecar so vanilla BlueBrick still opens the .bbm.
    Sidecar sidecar;

private:
    std::vector<std::unique_ptr<Layer>> layers_;
};

}
