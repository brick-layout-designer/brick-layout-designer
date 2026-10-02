#pragma once

// A module's picture for the Module library: the whole module, drawn the
// way Share picture draws a layout, about 96 px on its longest side (the
// web makes the same kind of picture when a module is saved). Kept in the
// cache folder, keyed by the file's path, size and time, so it's drawn
// again only when the module changes.

#include <QImage>
#include <QString>

namespace bld::parts { class PartsLibrary; }
namespace bld::core { class Map; }

namespace bld::ui {

inline constexpr int kModuleThumbnailSide = 96;

// The picture a module saved on a server gets, as the web makes it: about
// 1024 px on its longest side, PNG. The lists ask the server for a small one.
inline constexpr int kServerModuleThumbnailSide = 1024;

// Draws the picture of `module` (no cache). Null when it has no parts.
QImage renderModuleThumbnail(const core::Map& module, parts::PartsLibrary& parts, int side = kModuleThumbnailSide);

// Draws the picture of the module in `bbmPath` (no cache). Null when the
// file can't be read or has no parts.
QImage renderModuleThumbnail(const QString& bbmPath, parts::PartsLibrary& parts, int side = kModuleThumbnailSide);

// The cached picture, drawing and caching it first when it's missing or
// the file changed. `cacheDir` empty: the app's cache folder.
QImage moduleThumbnail(const QString& bbmPath, parts::PartsLibrary& parts, const QString& cacheDir = {},
                       int side = kModuleThumbnailSide);

}  // namespace bld::ui
