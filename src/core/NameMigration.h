#pragma once

// The app was called "Brick Layout Designer" before it became "Collaborative
// Brick Layout Designer". QSettings and QStandardPaths file everything under
// the organization and application names, so the new names start with empty
// settings and an empty data folder (live caches, imports, server parts,
// venues, modules, the autosave). migrateFromName() carries them over once,
// at startup, and leaves the old ones usable so an older version still
// works after a downgrade.

#include <QString>
#include <QVariant>

namespace bld::core {

struct AppName {
    QString organization;
    QString application;
};

// What the app is called now, and what it was called before.
AppName currentAppName();
AppName legacyAppName();

struct NameMigrationReport {
    bool alreadyDone = false;     // an earlier start already migrated
    bool settingsCopied = false;  // the old settings were copied (the old store stays)
    bool dataMoved = false;       // the data folder was renamed; the old path links to it (not on Windows)
    bool dataCopied = false;      // the data folder was copied; the old one stays
    QString error;                // what went wrong, if anything; the app goes on either way
};

// The settings key that marks the migration done.
inline constexpr const char* kNameMigrationDoneKey = "migration/renamedFromBrickLayoutDesigner";

// Carry the settings and the data folder of `from` over to the names
// QCoreApplication has now. Call once at startup, after setting the names
// and before anything reads QSettings. Does nothing on later starts, and
// never overwrites settings or data the new names already have.
NameMigrationReport migrateFromName(const AppName& from);

// `value` with the path prefix `from` replaced by `to` wherever a string (in
// a string, a list, or a JSON byte array) starts with it as a whole folder.
QVariant rewritePathPrefix(const QVariant& value, const QString& from, const QString& to);

}  // namespace bld::core
