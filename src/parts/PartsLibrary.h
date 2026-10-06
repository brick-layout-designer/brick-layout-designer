#pragma once

#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace bld::parts {

// Connection types are STRING ids in BlueBrick's part XMLs — e.g.
// "rail", "road", "coaster", "monorail", plus a growing long tail of
// domain-specific types (4DBrix, TrixBrix, etc.). Only same-type points
// may connect. An empty string means "no type" / never connects.
//
// Connection points are stored in each part's LOCAL coord system (in studs,
// with (0,0) at the part's centre). angleDegrees points "outward" — two
// connected points should face 180° apart.
struct PartConnectionPoint {
    QString type;
    QPointF position;
    double  angleDegrees = 0.0;
    // Electric-plug polarity: +1 or -1 mark the two 9V rails on a
    // track piece; 0 (the default) means no electrical signal. Matches
    // BlueBrick's sign convention exactly — a circuit exists inside a
    // part wherever plug[i] == -plug[j] and both are non-zero.
    int     electricPlug = 0;
    // <nextConnexionPreference>: the connection that becomes active when
    // this one gets linked (BlueBrick's chaining order for placing track).
    int nextPreferredIndex = 0;
};

// A within-part electrical circuit: the pair of connection indices
// whose plugs are opposite polarity (+1 and -1), plus the straight-line
// distance between them in studs (used to compute the line direction).
struct ElectricCircuit {
    int   index1 = 0;
    int   index2 = 0;
    float distanceStuds = 0.0f;
};

// One entry in the parts library. Matches the BlueBrick file-naming convention:
// each part is a `<PartNumber>.<ColorCode>.xml` paired with a matching `.gif`.
// Leaf parts live under <part>; grouped composite parts under <group>. Both
// share the same filename convention — we capture the kind here.
enum class PartKind {
    Leaf,
    Group,
};

struct PartDescription {
    QString language;  // e.g. "en", "fr"
    QString text;
};

// A single child of a set (<group>). BlueBrick's SubPart carries a
// subpart key (matches another part's library key), a local position in
// studs (offset from the set's reference origin), and an orientation
// in degrees.
struct PartSubPart {
    QString subKey;           // e.g. "TS_TRACK18S.8"
    QPointF position;         // studs, in set-local coords
    double  angleDegrees = 0.0;
};

struct PartMetadata {
    QString  partNumber;
    QString  colorCode;
    PartKind kind = PartKind::Leaf;
    QString  xmlFilePath;
    QString  gifFilePath;   // may be empty if the image is missing
    QString  author;
    QString  sortingKey;
    QList<PartDescription>     descriptions;
    QList<PartConnectionPoint> connections;  // from <ConnexionList> in part XML
    // Within-part electrical circuits: pairs of connection indices whose
    // electricPlug values are opposite (+1/-1). Computed once at parse
    // time by buildElectricCircuits(); empty for non-electric parts.
    QList<ElectricCircuit>     electricCircuits;
    // Populated only when kind == Group — the parts that make up the
    // set (from <SubPartList> in the XML).
    QList<PartSubPart>         subparts;
    // <CanUngroup> of a set: false when it may never be split (flex.group).
    bool                       canUngroup = true;

    // Pixels per stud the GIF was authored at. Vanilla BlueBrick parts
    // implicitly use 8; our LDD/LDraw imports may use a higher value
    // (16, 24, 32) to keep small detail visible. Read from the
    // <PixelsPerStud> element in the part XML; defaults to 8 when
    // absent so existing libraries keep working unchanged.
    int      pxPerStud = 8;

    // <LDraw> remap block: how the LDraw part's origin/orientation map
    // onto this part's image centre, as BlueBrick's LDraw loader applies
    // it. Angle in degrees; translation in LDU.
    double   ldrawAngle = 0.0;
    QPointF  ldrawTranslation;
    double   ldrawPreferredHeight = 0.0;  // LDU; used when saving at altitude 0
    QString  ldrawSleeper;   // "<part>.<colour>" LDraw sleeper added under rails on save
    QString  ldrawAlias;     // "<part>[.<colour>]" to write instead of this part

    // Where an imported part came from (<ImportSource>, fork-only).
    struct ImportSource {
        QString   path;
        QDateTime modified;
        int       quarterTurns = 0;
        QList<QPointF> droppedConnections;
    };
    std::optional<ImportSource> importSource;

    // Earlier part numbers (<OldNameList>); files using them load as this part.
    QStringList oldNames;

    // <hull> outline in sprite pixels (half-pixel centred, as BlueBrick
    // reads it). Empty: the hull is the sprite's bounding box.
    QList<QPointF> xmlHullPx;

    // <TrackDesigner> remap: how this part maps to TrackDesigner (.tdl).
    struct TrackDesignerPort {
        int   bbConnectionIndex = 0;  // BlueBrick connection used as the TD origin
        int   type = 20;              // TD piece type (0 straight, 1 left curve, ... 20 custom)
        float angleDifference = 0.0f; // TD angle - BlueBrick angle
    };
    struct TrackDesigner {
        int defaultId = 0;
        QHash<QString, int> registryIds;  // TD "registry" (part set) -> id
        int  flags = 0;
        bool hasSeveralPorts = false;
        QList<TrackDesignerPort> ports;
        int id(const QString& registry = QStringLiteral("default")) const {
            return registryIds.value(registry, defaultId);
        }
    };
    std::optional<TrackDesigner> trackDesigner;

    // <FourDBrix> remap: how this part maps to 4DBrix nControl (.ncp).
    struct FourDBrix {
        enum class Type { Segment, Table, Baseplate, Structure };
        Type    type = Type::Segment;
        QString partName;                  // segment name or svg path in nControl
        float   orientationDifference = 0.0f;
        int     originConnection = 0;      // connection used as the segment origin
    };
    std::optional<FourDBrix> fourDBrix;

    // BlueBrick's "ignorable" parts: a leaf part with an XML but no image,
    // e.g. the sleeper plates that LDraw exports put under 12V/4.5V rails.
    // They are skipped when loading LDraw files and never listed.
    bool isIgnorable() const { return kind == PartKind::Leaf && gifFilePath.isEmpty(); }
};

class PartsLibrary {
public:
    void addSearchPath(const QString& path);
    const QStringList& searchPaths() const { return searchPaths_; }

    // Walk every search path recursively, indexing every `<PartNumber>.<Color>.xml`
    // pair found. Returns the number of parts indexed. Safe to call repeatedly —
    // subsequent calls append new parts without clearing the index.
    int scan();

    // Index a single .xml file into the library. Returns the inserted key
    // (lower-cased "<partNum>.<colorCode>" or just "<partNum>") on success,
    // or an empty string if the file isn't a valid part XML or is already
    // indexed. Used by the importer to register a freshly-written library
    // part without re-scanning every search path on the UI thread.
    QString scanFile(const QString& xmlPath);

    int partCount() const { return static_cast<int>(index_.size()); }

    // Lookup key is "<PartNumber>.<ColorCode>" (e.g. "3001.1" or "TS_TRACK18S.8").
    // Old part numbers (<OldNameList>) resolve to the part that replaced them.
    std::optional<PartMetadata> metadata(const QString& key) const;

    // The part TrackDesigner id `tdId` maps to (BlueBrick's preference: the
    // current registry's id, then a default id, then any), or empty.
    QString partForTrackDesignerId(int tdId, const QString& registry = QStringLiteral("default")) const;

    // The part a 4DBrix name maps to, or empty. Case-sensitive, as in BlueBrick.
    QString partForFourDBrixName(const QString& name) const;

    // The current key for `key`: itself, or the part an old name maps to.
    // Empty when unknown.
    QString canonicalKey(const QString& key) const;
    QStringList keys() const;

    // Lazy-load a decoded QPixmap for a part. Returns a null QPixmap if the
    // part isn't indexed or its .gif is missing.
    QPixmap pixmap(const QString& key);
    // Whether pixmap(key) has been tried already (loaded or not), so it
    // returns without touching the disk.
    bool pixmapTried(const QString& key) const;
    // Forget a loaded (or failed) picture so the next pixmap() reads it again.
    void forgetPixmap(const QString& key);

    // Convex hull of the part's opaque pixels, in part-local STUD coords
    // (origin at the pixmap centre, x right, y down). Cached per-key.
    // Empty QPolygonF if the part has no pixmap or no opaque pixels —
    // callers fall back to the brick's displayArea rect in that case.
    //
    // Used for selection-shape hit testing and the "Display Hulls"
    // render toggle so irregular parts (curves, switches) outline
    // their real silhouette rather than a loose bounding rect.
    QPolygonF hullPolygonStuds(const QString& key);

    // BlueBrick's footprint of a part at an orientation: a brick's
    // displayArea is the box around its rotated hull (<hull> from the XML,
    // else the sprite's bounds), and the sprite is drawn `imageOffset`
    // studs from that box's centre (mOffsetFromOriginalImage). Non-zero
    // only for parts with an XML hull; up to ~3 studs for 9V switches.
    struct Footprint {
        QPointF imageOffset;  // studs, from displayArea centre to sprite centre
        QSizeF  size;         // studs, displayArea size
        // studs, from displayArea's top-left to the rotated image's top-left
        // corner (BlueBrick's TopLeftCornerPositionInStud).
        QPointF imageCorner;
    };
    std::optional<Footprint> footprint(const QString& key, double orientationDegrees);
    // footprint().imageOffset, cheaply: zero for the (most) parts without
    // an XML <hull>.
    QPointF imageOffset(const QString& key, double orientationDegrees);

    void clear();

    // Drop one part (and its cached sprite / hull) so scanFile() can
    // index a replaced version of it.
    void forget(const QString& key);

private:
    QStringList searchPaths_;
    QHash<QString, PartMetadata> index_;
    QHash<QString, QString>      renamed_;   // lower-cased old name -> current key
    QHash<int, QStringList>      trackDesignerIds_;  // TD id -> keys using it
    QHash<QString, QString>      fourDBrixNames_;    // 4DBrix name -> key
    QHash<QString, QPixmap>      pixmapCache_;
    QHash<QString, QPolygonF>    hullCache_;
};

}
