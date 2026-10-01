#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace bld::core { struct Sidecar; struct SavedView; }

namespace bld::saveload {

struct SidecarLoadResult {
    bool ok = false;
    QString error;
    bool hashMismatch = false;   // true if the stored hash differs from the current .bbm
};

// Compute the canonical sidecar path for a .bbm: "foo.bbm" -> "foo.bbm.bld".
QString sidecarPathFor(const QString& bbmPath);

// Read the sidecar at `cldPath` and populate `out`. If `bbmBytes` is non-empty,
// verify that its SHA-256 matches the hash stored in the sidecar; report a
// mismatch in the result without failing the load (caller decides how to react).
SidecarLoadResult readSidecar(const QString& cldPath,
                               const QByteArray& bbmBytes,
                               core::Sidecar& out);

// Write the sidecar JSON. `bbmBytes` is the just-written .bbm content used to
// stamp the hash so we can detect drift on next load.
bool writeSidecar(const QString& cldPath,
                  const QByteArray& bbmBytes,
                  const core::Sidecar& sidecar,
                  QString* error = nullptr);

// The sidecar as JSON and back, without the file or its hash: the same
// shape the web keeps in a live layout's shared document (meta.cache).
QJsonObject sidecarToJson(const core::Sidecar& sidecar);
void sidecarFromJson(const QJsonObject& root, core::Sidecar& out);

// One saved view as JSON and back (references/LAYOUT-FILE.md "Saved
// views"). Unknown fields ride along in SavedView::extras. A fit view's
// rect is written as null. viewFromJson returns nothing for a value that
// isn't a view at all (no id).
QJsonObject viewToJson(const core::SavedView& view);
std::optional<core::SavedView> viewFromJson(const QJsonValue& value);

// Compute SHA-256 of the bytes (hex-encoded lowercase).
QByteArray sha256Hex(const QByteArray& bytes);

}
