#pragma once

// "Add parts from a zip": a parts folder sent as a ZIP (such as Brick Track
// Designer's "Layout parts" export: README.txt beside one folder named after
// the design) unpacked into a parts folder, keeping its folder. The archive
// is untrusted: every name goes through resolveArchiveEntryPath (no "../",
// no absolute paths), symbolic links are refused, and the number of entries
// and their sizes are capped before anything is written.

#include <QString>
#include <QStringList>

namespace bld::import {

struct PartsZipLimits {
    qint64 maxArchiveBytes = qint64{ 100 } * 1024 * 1024;
    int maxEntries = 4000;
    qint64 maxEntryBytes = qint64{ 32 } * 1024 * 1024;
    qint64 maxTotalBytes = qint64{ 256 } * 1024 * 1024;
};

struct PartsZipResult {
    bool ok = false;
    QString error;        // in plain words, when !ok
    QStringList folders;  // the folders written under the destination, e.g. "Yard"
    QStringList partKeys; // the parts and sets in them, by key: "Yard.track-plate1", "Yard.whole"
};

// Unpacks the ZIP at `zipPath` under `destRoot`. Files inside a top-level
// folder keep their path; files at the top level (a README) are left out,
// unless the ZIP has no folder at all, when they go into a folder named
// after the ZIP. Nothing is written unless the whole archive passes the
// checks and holds at least one part (.xml). Files already there are
// replaced, as unzipping over an older export does.
PartsZipResult addPartsFromZip(const QString& zipPath, const QString& destRoot,
                               const PartsZipLimits& limits = {});

} // namespace bld::import
