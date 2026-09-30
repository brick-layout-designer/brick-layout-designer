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
    QStringList differing;  // part keys the library already has with other XML: nothing is written for them
    QStringList failed;     // file names that couldn't be written
};

// Writes the parts in `files` that `library` doesn't have to `dir`. A part
// the library already has keeps the library's definition.
LayoutPartsInstall installLayoutParts(const QMap<QString, QByteArray>& files, const QString& dir,
                                      const parts::PartsLibrary& library);

// One part's files in `files` (a layout's parts, by file name): the XML and
// sprites named <key> or, for a set, <key>.set.
QMap<QString, QByteArray> filesOfPart(const QMap<QString, QByteArray>& files, const QString& key);

// The library's files for `key`: its XML and the sprites beside it, by file name.
QMap<QString, QByteArray> libraryFilesOfPart(const parts::PartsLibrary& library, const QString& key);

// "Use the layout's": copies the part at `localXmlPath` (the XML and its
// sprites) into `backupDir`, then writes the layout's `files` for that part
// in its place under the same names. Sprites of the old part that the
// layout's lacks are removed (they're in the backup). False, with `error`
// set, when a file couldn't be backed up or written; nothing is removed
// before the backup is complete.
bool replaceLocalPart(const QString& localXmlPath, const QMap<QString, QByteArray>& files,
                      const QString& backupDir, QString* error = nullptr);

// A part key like `key` the library doesn't have: <PartNumber>-2.<Color>,
// then -3 and on.
QString unusedPartKey(const QString& key, const parts::PartsLibrary& library);

// "Keep both": writes the files in `files` named <stem>.* to `dir` as
// <newStem>.*. For a set, `stem` and `newStem` end in ".set". Returns the
// XML path written, or empty when nothing could be.
QString installPartAs(const QMap<QString, QByteArray>& files, const QString& stem, const QString& newStem,
                      const QString& dir);

// Points the bricks and library groups of part `from` at `to` (keys match
// without case). Returns how many changed.
int renamePartInMap(core::Map& map, const QString& from, const QString& to);

}  // namespace bld::import
