#pragma once

// The y-websocket wire format the collaborative server speaks on
// /ws/layout/:id (y-protocols sync + awareness, lib0 encoding): a
// varuint message type, then for sync messages a varuint step and a
// length-prefixed payload.
//
//   [0][0][len][state vector]   sync step 1: "send me what I'm missing"
//   [0][1][len][update]         sync step 2: the reply
//   [0][2][len][update]         an update (a change)
//   [1][len][awareness update]  cursors / selections

#include <QByteArray>

#include <optional>

namespace bld::sync::protocol {

enum class Kind { SyncStep1, SyncStep2, Update, Awareness };

struct Message {
    Kind kind;
    QByteArray payload;  // state vector, update, or awareness update
};

QByteArray encode(Kind kind, const QByteArray& payload);

// nullopt for truncated or malformed messages and unknown types (which
// the server, like y-websocket, ignores).
std::optional<Message> decode(const QByteArray& message);

}  // namespace bld::sync::protocol
