#pragma once

// The venue as JSON, the one encoding for .bld-venue files, the sidecar's
// `venue`, a live layout's meta.cache and the server's venue library
// (the web repo's references/VENUE-MODEL.md). Optional parts are written
// only when set; unknown fields come back from `extras`.

#include "../core/Venue.h"

#include <QJsonObject>

namespace bld::saveload {

QJsonObject venueToJson(const core::Venue& venue);
core::Venue venueFromJson(const QJsonObject& o);

} // namespace bld::saveload
