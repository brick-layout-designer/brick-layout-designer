#pragma once

// Presence on a live layout (y-protocols awareness, lib0 encoding): who is
// there, where their cursor is and what they have selected, in the web
// editor's state shape (apps/web/src/editor/awareness.ts):
//   { user: {id, name, color}, cursor: {x, y, layerId?} | null,
//     selection: {brickIds: [...]}, tool, lastActivityMs }
// An update is a varuint count, then per client its id, clock and state as
// a JSON string ("null" when it left).

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>

#include <optional>

namespace bld::sync::awareness {

struct Entry {
    quint32 clientId = 0;
    quint32 clock = 0;
    std::optional<QJsonObject> state; // nullopt: the client left
};

QByteArray encode(const QList<Entry>& entries);
// nullopt for truncated or malformed updates.
std::optional<QList<Entry>> decode(const QByteArray& update);

// Other people's states, as y-protocols keeps them: a newer clock wins, a
// null state removes the client, and our own id is ignored.
class Peers {
public:
    explicit Peers(quint32 ownId) : ownId_(ownId) {}
    // Returns whether anything changed.
    bool apply(const QByteArray& update);
    void clear() {
        states_.clear();
        clocks_.clear();
    }
    const QHash<quint32, QJsonObject>& states() const { return states_; }

private:
    quint32 ownId_;
    QHash<quint32, QJsonObject> states_;
    QHash<quint32, quint32> clocks_;
};

} // namespace bld::sync::awareness
