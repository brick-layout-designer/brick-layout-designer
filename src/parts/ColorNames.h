#pragma once

#include <QString>

namespace bld::parts {

// The name of LDraw colour `code` ("Black", "Light Bluish Gray"...) from
// BlueBrick's ColorTable.xml (bundled), in `language` ("en", "fr", "de")
// with English as the fallback. Empty for an unknown or non-numeric code.
QString colorName(const QString& code, const QString& language = QStringLiteral("en"));

}  // namespace bld::parts
