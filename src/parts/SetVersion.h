#pragma once

// A custom set's version: which contents a placed copy of it was made from,
// so a layout can say "Update available" once the set changes (its parts,
// where they sit, or its name). The web's server makes the same string from
// the same XML (apps/server/src/utils/setVersion.ts; shared vectors in both
// apps' tests), so a copy placed on one is known on the other:
//
//   "set-v1\n" + English name (trimmed) + "\n"
//   + for each sub-part, in order: lower-cased id "|" x "|" y "|" angle "\n"
//
// with each number as floor(v * 1000 + 0.5); the version is the first 16
// hex digits of that text's SHA-256 (UTF-8).
//
// A set holding custom sets also follows theirs, so a change inside one
// shows "Update available" on the set around it too (edit::trackedSetVersion
// finds them). Its version is
//
//   "set-v2\n" + its own version above + "\n"
//   + for each sub-part that is a custom set, in order:
//     lower-cased id "|" that set's version (this one, so nested ones count) "\n"
//
// digested the same way. A set with no custom set inside keeps its own
// version, so copies placed before this still match.

#include "PartsLibrary.h"

#include <QList>
#include <QString>

namespace bld::parts {

QString setVersionText(const PartMetadata& set);
// Empty for a part that isn't a set.
QString setVersion(const PartMetadata& set);

// A custom set inside a set: its sub-part id and its version.
struct InnerSetVersion {
    QString subKey;
    QString version;
};
// A set's version from its own (setVersion) and those of the custom sets
// inside it, in order; its own when there are none.
QString nestedSetVersion(const QString& own, const QList<InnerSetVersion>& inner);

} // namespace bld::parts
