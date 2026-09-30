#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

namespace bld::core { class Map; }

namespace bld::import {

// The native layout file (.bld-layout): the whole layout in one file, so a
// save never needs a sidecar next to it. A ZIP holding
//   manifest.json      {"format":"bld-layout","version":1,"generator":...}
//   layout.bbm         the map as BlueBrick writes it (BbmWriter)
//   sidecar.json       labels, modules, venue, background (sidecarToJson), if any
//   background.<ext>   the background image, which sidecar.json names as "file"
// The web app reads and writes the same file (references/LAYOUT-FILE.md).

inline constexpr int kLayoutFileVersion = 1;

bool isLayoutFile(const QString& path);

struct LayoutFileResult {
    std::unique_ptr<core::Map> map;
    QString error;        // why the file couldn't be read
    QStringList warnings; // read, but with something left out
    bool ok() const { return map != nullptr; }
};

// `assetDir` is where an embedded background image is unpacked (named by its
// hash, so unpacking the same image twice gives the same file).
LayoutFileResult readLayoutFile(const QString& path, const QString& assetDir);
LayoutFileResult readLayoutFileBytes(const QByteArray& bytes, const QString& assetDir);

// The file's bytes: the background image is read from its path and embedded
// (a warning names it when it can't be read). Empty on failure.
QByteArray layoutFileBytes(const core::Map& map, QString* error, QStringList* warnings = nullptr);
bool writeLayoutFile(const core::Map& map, const QString& path, QString* error,
                     QStringList* warnings = nullptr);

}  // namespace bld::import
