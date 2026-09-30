#pragma once

// Presence in the web editor's shape (apps/web/src/editor/awareness.ts),
// built and read the way the web does so cursors look the same in both:
//   { user: {id, displayName, avatarUrl, color}, cursor: {x, y, layerId} | null,
//     selection: {brickIds}, tool, lastActivityMs }

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

namespace bld::sync::presence {

// The web's deterministicColor: the same colour for a user on a layout everywhere.
QString colorFor(const QString& userId, const QString& layoutId);

struct User {
    QString id;
    QString displayName;
    QString color;
};

QJsonObject state(const User& user, std::optional<QPointF> cursorStuds, const QStringList& brickIds,
                  qint64 nowMs);

// Someone else, read from their state (missing parts are empty).
struct Peer {
    QString name;
    QString color;
    std::optional<QPointF> cursor;
    QStringList brickIds;
};
Peer peerFrom(const QJsonObject& state);

// The on-map label: names over 20 characters are cut with an ellipsis, as on the web.
QString label(const QString& name);

} // namespace bld::sync::presence
