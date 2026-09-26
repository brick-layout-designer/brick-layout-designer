#pragma once

#include <QString>

class QImage;

namespace bld::import {

// Write `image` as a single-frame GIF89a. Qt ships a GIF reader but no
// writer; vanilla BlueBrick only loads part sprites from .gif files, so
// imported parts also get an 8 px/stud .gif next to their hi-res .png.
//
// Pixels with alpha < 128 become the transparent colour; the rest are
// reduced to at most 255 colours (exact when the image has that few,
// median cut otherwise). Returns false and sets *error on failure.
bool writeGif(const QImage& image, const QString& path, QString* error = nullptr);

}  // namespace bld::import
