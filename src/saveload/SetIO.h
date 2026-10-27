#pragma once

// Writer for BlueBrick / BrickTracks / TrixBrix `.set.xml` files. A set is
// a `<group>` of subparts with per-subpart position + angle (no explicit
// connectivity — connection graph is reconstructed from positions at
// placement time by the consuming app, e.g. our addPartAtScenePos set
// branch).
//
// Format reference: the subparts under parts/BrickTracks/*.set.xml and
// parts/TrixBrix/*.set.xml that ship with BlueBrick 1.9.2, which this
// writer mirrors. Emitting exactly the same schema means sets authored
// in this app drop straight into vanilla BlueBrick's parts library.

#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace bld::saveload {

struct SetSubpart {
    QString partKey;       // e.g. "TB R104.8"
    QPointF positionStuds; // rotated-hull-bbox centre, set-local
    double  angleDegrees;  // as stored in the set XML (not normalised)
};

struct SetManifest {
    QString name;                    // <Description><en>…</en></Description>
    QString author;                  // optional
    QString sortingKey;              // optional
    bool    canUngroup = true;
    QList<SetSubpart> subparts;
};

// Writes the set XML to `filePath`. Returns true on success, false with a
// message in `error` otherwise. Caller owns the path and is responsible for
// choosing an appropriate `.set.xml` filename under a parts library folder.
bool writeSetXml(const QString& filePath,
                 const SetManifest& manifest,
                 QString* error = nullptr);

// Renames the set in `filePath`: its English name (<Description><en>) becomes
// `name`, and the rest of the file stays as it was, so its file name (its part
// number) and every layout that uses it are unchanged. A set without a
// <Description> gets one. False with a message in `error` when the file can't
// be read, isn't a set, or can't be written.
bool renameSetXml(const QString& filePath, const QString& name, QString* error = nullptr);

// `xml` (a set's) with its English name set to `name`, the rest as it was;
// none when it isn't a set. renameSetXml does this to a file; the desktop
// renames a set's copy on a server with it.
std::optional<QString> renamedSetXml(const QString& xml, const QString& name);

// The file name (and part number) a set named `name` is saved under,
// without ".set.xml": the characters a file name can't hold become "_",
// and leading or trailing dots and spaces go (the web's setPartNumber).
QString setFileStem(const QString& name);

// The sub-part among `subpartKeys` (a set's part numbers, as its XML names
// them) that a set named `name` would share its number with, or empty. The
// library keys a set by its file name without ".set.xml", so such a set
// would stand in for that part, and placing it would nest copies of itself:
// the name is refused.
QString partNamedLikeSet(const QString& name, const QStringList& subpartKeys);

// The part numbers the set file `filePath` names (its <SubPart id>s), as
// written; empty when it can't be read.
QStringList setSubpartKeys(const QString& filePath);

// A name like `name` that isn't taken: "Yard (2)", "Yard (3)", … (and
// "Yard (2)" goes on to "Yard (3)"). `taken` says whether a file stem
// (setFileStem) is already used.
QString freeSetName(const QString& name, const std::function<bool(const QString& stem)>& taken);

}  // namespace bld::saveload
