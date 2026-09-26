#pragma once

#include <QString>

namespace bld::import {

// Absolute destination path for archive entry `entryName` extracted under
// `destRoot`, or an empty string when the entry must not be written: empty
// names, absolute / drive-qualified paths, and any "../" that would land
// outside `destRoot` ("zip-slip"). Every extractor (Download Center,
// LDraw library download, LIF) routes entry names through this before
// touching the filesystem — archives come from the network or from
// user-supplied files and are untrusted.
QString resolveArchiveEntryPath(const QString& destRoot, const QString& entryName);

}  // namespace bld::import
