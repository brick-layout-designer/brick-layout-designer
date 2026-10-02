#pragma once

// A server module's contents: a shared document with the same shape as a
// layout's (the web's seedFromBbm / docToBbm). Reading one gives a map whose
// parts sheets become the inserted module; saving one writes the module's
// map into the document the server already holds (a new module starts with
// the server's empty layout), changing only what differs, so the web reads
// it back exactly as the web itself would have saved it.

#include <QByteArray>
#include <QString>

#include <memory>

namespace bld::core { class Map; }

namespace bld::sync {

// nullptr (and *error set) when the snapshot isn't a readable layout.
std::unique_ptr<core::Map> mapFromModuleSnapshot(const QByteArray& snapshot, QString* error = nullptr);

// The document to save: `current` (the module's contents on the server,
// possibly empty) made to hold `module`. Empty (and *error set) when
// `current` can't be read.
QByteArray moduleSnapshotFor(const core::Map& module, const QByteArray& current, QString* error = nullptr);

// How many parts the map has on its parts sheets.
int partCount(const core::Map& map);

}  // namespace bld::sync
