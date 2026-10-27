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

#include "PartsLibrary.h"

#include <QString>

namespace bld::parts {

QString setVersionText(const PartMetadata& set);
// Empty for a part that isn't a set.
QString setVersion(const PartMetadata& set);

} // namespace bld::parts
