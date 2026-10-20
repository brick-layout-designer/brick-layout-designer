#include "VenueIO.h"
#include "VenueJson.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace bld::saveload {

bool writeVenueFile(const QString& path, const core::Venue& v, QString* errOut) {
    QJsonObject root = venueToJson(v);
    root[QStringLiteral("schema")] = QStringLiteral("bld-venue/1");

    // All or nothing: a failed save leaves the file that was there.
    QSaveFile f(path);
    const QByteArray bytes = QJsonDocument(root).toJson();
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (errOut) *errOut = f.errorString();
        return false;
    }
    return true;
}

std::optional<core::Venue> readVenueFile(const QString& path, QString* errOut) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errOut) *errOut = f.errorString();
        return std::nullopt;
    }
    QJsonParseError jerr;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        if (errOut) *errOut = jerr.errorString();
        return std::nullopt;
    }
    return venueFromJson(doc.object());
}

}
