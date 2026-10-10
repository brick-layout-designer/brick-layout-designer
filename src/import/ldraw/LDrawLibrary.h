#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace bld::import {

// Locates and resolves files inside a user-pointed LDraw library.
//
// LDraw distributes parts as plain-text .dat files under a root that
// has this canonical layout:
//
//   <root>/
//     LDConfig.ldr         - color palette (mandatory for our renderer)
//     parts/
//       <partid>.dat       - top-level parts (e.g. 3001.dat for a 2x4 brick)
//       s/<partid>.dat     - sub-parts (vendor-internal split files)
//     p/
//       <name>.dat         - primitives shared across many parts
//       8/<name>.dat       - low-resolution primitive variants
//       48/<name>.dat      - high-resolution primitive variants
//     models/              - example models, ignored here
//
// `resolve()` returns the absolute on-disk path for a given `.dat`
// reference following LDraw's stock search order (parts/, p/, p/48/,
// p/8/, parts/s/, then the same under Unofficial/ when the library has
// it, as Studio's does, then root). When nothing matches, returns an empty string
// and the caller treats the reference as "unknown — skip".
//
// Studio (.io) uses an LDraw library too — typically the bundled one
// at <Studio install>/Studio2/ldraw, sometimes the user's standard
// install. Either path works as the `root` here; we don't care which
// vendor produced the files as long as the layout matches.
class LDrawLibrary {
public:
    LDrawLibrary() = default;
    explicit LDrawLibrary(QString root);

    const QString& root() const { return root_; }

    // Folders searched before the library, for parts that come with the
    // model (a Studio file's CustomParts). Clears the resolve cache.
    void setOverlayDirs(QStringList dirs);

    // True when the root looks like a real LDraw install — has
    // LDConfig.ldr at top level AND a parts/ subdirectory. Good enough
    // for a sanity check before we accept user-pointed paths.
    bool looksValid() const;

    // Resolve a `.dat` reference (case-insensitive on the filename
    // component, since LDraw filenames mix cases on disk but author
    // tools normalise to upper or lower depending on era). Returns the
    // absolute path or an empty string when the reference cannot be
    // satisfied. Resolved answers are memoised, so repeated lookups
    // for the same name are O(1) — important for the bake pipeline
    // which walks tens of thousands of subfile refs across LDraw's
    // primitive set.
    QString resolve(const QString& filename) const;

    // Earlier numbers of a renumbered part, newest first, lower-case and
    // without ".dat" (e.g. "74746" -> {"2865"}). LDraw keeps a stub
    // "0 ~Moved to <new>" file under the old number, so this is how a
    // model using current numbers finds a library that still uses the
    // old ones (BlueBrickParts' 9V track is 2865/2867). Chains are
    // followed. The index is built on first call by reading the header
    // line of every parts/*.dat.
    QStringList formerNames(const QString& partName) const;

private:
    // Lower-cased filename → absolute path index, built lazily per
    // search directory. The index is mutable because resolve() is
    // logically const from the caller's POV but populates the cache
    // on first miss for each directory.
    using FileIndex = QHash<QString, QString>;
    const FileIndex& indexForSubdir(const QString& subdir) const;

    // `subdir` under `base` (root_ when empty), the lower-case file name
    // index of that directory.
    const FileIndex& indexFor(const QString& base, const QString& subdir) const;
    // The library's unofficial-parts folder ("Unofficial", Studio's
    // "UnOfficial"), relative to the root; empty without one.
    const QString& unofficialDir() const;

    QString root_;
    QStringList overlays_;
    mutable QHash<QString, FileIndex> indexBySubdir_;
    mutable std::optional<QString> unofficial_;
    // Resolved-result cache so e.g. "stud.dat" referenced 50 000 times
    // only walks the search dirs once.
    mutable QHash<QString, QString> resolveCache_;

    // new part name -> names that were moved to it (lower-case, no .dat).
    mutable QHash<QString, QStringList> movedFrom_;
    mutable bool movedFromBuilt_ = false;
};

}  // namespace bld::import
