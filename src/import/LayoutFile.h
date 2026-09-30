#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::import {

// The native layout file (.bld-layout): the whole layout in one file, so a
// save never needs a sidecar next to it. A ZIP holding
//   manifest.json      {"format":"bld-layout","version":1,"generator":...}
//   layout.bbm         the map as BlueBrick writes it (BbmWriter)
//   sidecar.json       labels, modules, venue, background (sidecarToJson), if any
//   background.<ext>   the background image, which sidecar.json names as "file"
//   parts/<file>       the parts the layout uses that aren't in the standard
//                      library: each <PartNumber>.<Color>.xml and its sprites
// The web app reads and writes the same file (references/LAYOUT-FILE.md).

inline constexpr int kLayoutFileVersion = 1;

bool isLayoutFile(const QString& path);

struct LayoutFileResult {
    std::unique_ptr<core::Map> map;
    QString error;        // why the file couldn't be read
    QStringList warnings; // read, but with something left out
    // The parts the file carries, by file name (without "parts/").
    QMap<QString, QByteArray> partFiles;
    bool ok() const { return map != nullptr; }
};

// `assetDir` is where an embedded background image is unpacked (named by its
// hash, so unpacking the same image twice gives the same file).
LayoutFileResult readLayoutFile(const QString& path, const QString& assetDir);
LayoutFileResult readLayoutFileBytes(const QByteArray& bytes, const QString& assetDir);

// The file's bytes: the background image is read from its path and embedded
// (a warning names it when it can't be read). Empty on failure.
// `partFiles` (from layoutPartFiles) go in as parts/<name>.
QByteArray layoutFileBytes(const core::Map& map, QString* error, QStringList* warnings = nullptr,
                           const QMap<QString, QByteArray>& partFiles = {});
bool writeLayoutFile(const core::Map& map, const QString& path, QString* error,
                     QStringList* warnings = nullptr, const QMap<QString, QByteArray>& partFiles = {});

// The files of the parts `map` uses (sets with their subparts) that aren't
// under `standardRoot`, the bundled BlueBrick library: each part's XML and
// the sprites beside it, by file name.
QMap<QString, QByteArray> layoutPartFiles(const core::Map& map, const parts::PartsLibrary& library,
                                          const QString& standardRoot);

// A file name a layout file may carry under parts/: plain, with a part
// file's extension.
bool isLayoutPartFileName(const QString& name);

struct LayoutPartsInstall {
    QStringList newParts;   // XML paths written to `dir`, for the library to take in
    QStringList differing;  // part keys the library already has with other XML: the library's are kept
    QStringList failed;     // file names that couldn't be written
};

// Writes the parts in `files` that `library` doesn't have to `dir`. A part
// the library already has keeps the library's definition.
LayoutPartsInstall installLayoutParts(const QMap<QString, QByteArray>& files, const QString& dir,
                                      const parts::PartsLibrary& library);

}  // namespace bld::import
