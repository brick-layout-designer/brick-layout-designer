#pragma once

// A check on the bytes of a shared layout before yrs reads them.
//
// yrs (0.28) reserves memory for as many clients, blocks and values as an
// update or a state vector claims, before it looks at whether the bytes can
// hold them, and reads nested values by recursing once per level. A few
// damaged or hostile bytes from the network can therefore ask for tens of
// gigabytes, stall for seconds, or overflow the stack
// (https://github.com/y-crdt/y-crdt/issues/675). These checks walk the
// bytes the way yrs's update v1 decoder does, without allocating, and
// refuse anything yrs shouldn't be given:
//
// - more than kMaxUpdateBytes;
// - a count or length longer than the bytes left could hold;
// - values nested deeper than kMaxNesting;
// - content kinds yrs can't read, or a layout never holds (weak links);
// - strings with a NUL character in them (yffi panics on those).
//
// A refused update is never handed to yrs, so it can't be half applied.

#include <QByteArray>
#include <QString>

namespace bld::sync::guard {

// The largest update or state vector read. The biggest real layouts are a
// few MB as a shared document (a 540 KB .bbm is 250 KB); the server takes
// whole documents up to 50 MiB and messages up to 16 MiB. 64 MiB leaves room
// for a full resync of a layout far past either.
constexpr qsizetype kMiB = qsizetype{ 1024 } * 1024;
constexpr qsizetype kMaxUpdateBytes = 64 * kMiB;

// How deep values (lists and objects in a layout's JSON) may nest. The web's
// layout model nests 5 deep.
constexpr int kMaxNesting = 256;

// Empty when `update` (Yjs update v1, as y-websocket sends it) may go to
// yrs; otherwise why not, for the log.
QString checkUpdate(const QByteArray& update);

// The same for a state vector (sync step 1).
QString checkStateVector(const QByteArray& stateVector);

}  // namespace bld::sync::guard
