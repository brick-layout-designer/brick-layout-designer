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

}  // namespace bld::saveload
