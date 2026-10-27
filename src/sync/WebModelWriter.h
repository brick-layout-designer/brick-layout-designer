#pragma once

// core::Map → the web editor's layout model, the inverse of WebModel.h:
// the JSON shapes the shared document stores (meta, layer order, layer
// data with bricks, groups, text cells, areas and rulers).

#include <QHash>
#include <QJsonObject>
#include <QRectF>
#include <QString>

#include <utility>
#include <vector>

namespace bld::core {
class Map;
struct Sidecar;
} // namespace bld::core

namespace bld::sync {

// Parts as JSON, kept from one docJsonFromMap to the next: a part whose
// fields are what they were gets the same object back, and changed() says
// which ones didn't (SyncDoc skips comparing the others with the document,
// which on 5,000 parts was most of the time an edit took).
class PartJsonMemo {
public:
    // Whether the part with this id was made again by the last call (new,
    // changed, or more than one part with the id, then or the call before).
    bool changed(const QString& id) const;

    // (docJsonFromMap's)
    struct Fields {
        QString id, group, partNumber;
        double x = 0, y = 0, width = 0, height = 0;
        float orientation = 0, altitude = 0;
        int activeConnection = 0;
        std::vector<std::pair<QString, QString>> links;  // connection id, linked to
        bool operator==(const Fields&) const = default;
    };
    QJsonObject json(Fields&& fields);
    void startCall();
    void endCall(qsizetype parts);

private:
    struct Entry {
        Fields fields;
        QJsonObject json;
        quint64 seen = 0;  // the call it was last seen in
        bool changed = true;
        bool repeated = false;  // more than one part had the id in that call
    };
    QHash<QString, Entry> entries_;
    quint64 call_ = 0;
};

// { meta, layers, layerData } as the web's bbmToDoc lays them out. With a
// memo, parts unchanged since the last call with it come back as the same
// objects.
QJsonObject docJsonFromMap(const core::Map& map, PartJsonMemo* memo = nullptr);

// The shared document's sidecar cache (meta.cache: anchored labels, saved views,
// modules, venue) after the desktop's sidecar is written into `current`.
// Last write wins per part; parts the desktop doesn't handle (the web's
// background image) are kept, and an empty list stays absent.
QJsonObject mergeSidecarCache(const QJsonObject& current, const core::Sidecar& sidecar);

}  // namespace bld::sync
