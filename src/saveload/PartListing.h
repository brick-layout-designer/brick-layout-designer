#pragma once

// BlueBrick's <NotListedInLibrary> in a part's or set's XML: true keeps the
// part out of the parts list and from being placed by itself; it's there
// for the sets that use it. The web's server sets it the same way
// (apps/server/src/utils/partXml.ts withNotListed).

#include <QString>

namespace bld::saveload {

// `xml` with the tag set to true (`notListed`) or taken out; the rest as it was.
QString withNotListed(const QString& xml, bool notListed);

// The same, to the file at `filePath`. False with a message in `error` when
// it can't be read or written.
bool setNotListed(const QString& filePath, bool notListed, QString* error = nullptr);

}  // namespace bld::saveload
