#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace bld::parts { class PartsLibrary; }

namespace bld::import {

// Write a single BlueBrick part that represents an imported LDraw /
// Studio / LDD model as a flat top-down sprite, into `destLibraryDir`:
//   <key>.xml  part description (+ <PixelsPerStud> for hi-res sprites)
//   <key>.png  the sprite as given, when it isn't 8 px/stud
//   <key>.gif  8 px/stud sprite; the only file vanilla BlueBrick reads
// `widthStuds` x `heightStuds` is the footprint; the sprite's pixel size
// divided by it gives the resolution. The key is `sourceFilePath`'s stem;
// an existing part of that name is replaced when `replaceExisting`,
// otherwise the key gets a -2, -3, ... suffix.
//
// Returns the new part key (the filename stem without the extension),
// or an empty string on failure; the error is written to *error
// if non-null.
// One free-external connection on the composite, in sprite-local
// coords (origin = sprite centre, studs, y-down). Fed to
// writeImportedModelAsLibraryPart's new overload so the resulting
// library part is snap-compatible with other tracks/rails/etc.
struct ImportedConnection {
    QString type;              // BlueBrick connection type string ("rail", "road", etc.)
    double  xStuds   = 0.0;    // sprite-local x
    double  yStuds   = 0.0;    // sprite-local y
    double  angleDeg = 0.0;    // world-facing angle
};

// The part key (file stem) writeImportedModelAsLibraryPart derives from a
// source path or user-entered name, before any -2/-3 suffix.
QString importedPartKey(const QString& sourceFilePathOrName);

QString writeImportedModelAsLibraryPart(
    const QString& sourceFilePath,
    QImage         renderedSprite,
    int            widthStuds,
    int            heightStuds,
    const QString& destLibraryDir,
    const QString& authorName,
    QString*       error = nullptr);

// Variant that also writes a <ConnexionList> into the generated XML so
// the composite part is snap-compatible when placed. `connections`
// are in sprite-local coords (the same frame the XML's ConnexionList
// assumes). Empty list ⇒ equivalent to the no-conn overload above.
QString writeImportedModelAsLibraryPart(
    const QString& sourceFilePath,
    QImage         renderedSprite,
    int            widthStuds,
    int            heightStuds,
    const QString& destLibraryDir,
    const QString& authorName,
    const QVector<ImportedConnection>& connections,
    QString*       error = nullptr,
    bool           replaceExisting = false);

}  // namespace bld::import
