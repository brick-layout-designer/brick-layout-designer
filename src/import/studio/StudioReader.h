#pragma once

#include "../ldraw/LDrawReader.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace bld::import {

// BrickLink Studio 2.0 `.io` files are ZIP archives:
//   model.ldr     the model, LDraw text with LDraw colors; an MPD whose
//                 first "0 FILE" block is the model and the rest submodels
//   modelv1.ldr   the same in Studio's older "10 <color> False 0 ..." lines
//   model2.ldr    the same with BrickLink color numbers and every custom
//                 part's geometry inlined (big: 72 MB for a large set)
//   CustomParts/  the model's own parts (*.dat), with Studio-only
//                 connectivity/*.conn and collider/*.col next to them
//   thumbnail.png, errorPartList.err, .info
// Files from Studio 2.1 on encrypt every entry with ZipCrypto under Studio's
// fixed, widely published password (kStudioPasswords).
//
// We read model.ldr (falling back to another .ldr only when it is missing or
// empty) and unpack CustomParts/*.dat to a temporary folder listed in
// `extraPartDirs`, so the custom parts resolve when the model is rendered.
//
// Returns the same LDrawReadResult as readLDraw(). On failure `ok` is false
// and `error` says why in words for the user.
LDrawReadResult readStudioIo(const QString& path);

// The passwords tried on encrypted Studio entries, in order.
QList<QByteArray> studioPasswords();

}  // namespace bld::import
