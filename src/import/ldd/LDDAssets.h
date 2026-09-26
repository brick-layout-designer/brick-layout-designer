#pragma once

#include "../lif/LifReader.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace bld::import {

// LEGO Digital Designer's brick database (geometry .g files and
// Materials.xml) plus its ldraw.xml, located from whatever folder the
// user points at. LDD keeps these in different places depending on how
// it was installed, so `open()` accepts any of:
//   <root>/db.lif                  LDD's per-user data folder
//   <root>/Assets/db.lif
//   <root>/db/ or <root>/Assets/db/  a db.lif someone extracted
//   <root>/Assets.lif              LDD's program folder / installer
//                                  payload (db.lif nested inside)
// ldraw.xml is looked for in <root> and its parent.
class LDDAssets {
public:
    bool open(const QString& root);
    bool isOpen() const { return db_ || !dbDir_.isEmpty(); }

    // A database file by its path relative to the database root, e.g.
    // "Materials.xml" or "Primitives/LOD0/3001.g". Empty if missing.
    QByteArray read(const QString& relPath) const;

    // Path to ldraw.xml, or empty when none was found.
    const QString& ldrawXmlPath() const { return ldrawXml_; }

    // Where the database was found, for diagnostics.
    const QString& source() const { return source_; }

private:
    std::unique_ptr<LifReader> outer_;  // Assets.lif, when db.lif is nested
    std::unique_ptr<LifReader> db_;
    QString dbDir_;                      // extracted database folder
    QString ldrawXml_;
    QString source_;
};

}  // namespace bld::import
