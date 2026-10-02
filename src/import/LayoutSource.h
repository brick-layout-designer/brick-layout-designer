#pragma once

// Where a layout file came from (manifest.json "source"), and what a save
// writes into the manifest besides its own fields. Kept apart from
// LayoutFile.h so the main window can hold them without the map types.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace bld::import {

// The server layout a file was saved from: written when the desktop saves a
// live layout (or a file that already had one) and by every web download.
// Purely local layouts have none. Optional and additive: readers that don't
// know it ignore it. Kept as written, so a re-save writes it back unchanged.
struct LayoutSource {
    QString server;      // the server's base address: scheme://host[:port], no trailing slash
    QString layoutId;
    QString title;       // the layout's title when the file was saved
    QString exportedAt;  // when, as ISO 8601 (UTC, with milliseconds)

    // A manifest's "source", when it is a whole, sensible one.
    static std::optional<LayoutSource> fromJson(const QJsonValue& value);
    QJsonObject toJson() const;
    bool operator==(const LayoutSource&) const = default;
};

// The base address of a server ("https://x.org/a/" -> "https://x.org"), or
// empty when `url` isn't an http(s) address.
QString layoutServerBase(const QString& url);

// What a save writes into manifest.json besides its format, version and
// generator: the fields of a manifest read earlier (kept, so a newer
// version's fields survive a re-save) and the layout's source.
struct LayoutManifestExtras {
    QJsonObject keep;
    std::optional<LayoutSource> source;
};

}  // namespace bld::import
