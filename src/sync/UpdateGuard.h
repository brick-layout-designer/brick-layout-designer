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
// - strings with a NUL character in them (yffi panics on those);
// - more work to apply than kMaxUpdateWork (SyncDoc::applyUpdate, with
//   updateWork).
//
// A refused update is never handed to yrs, so it can't be half applied.

#include <QByteArray>
#include <QHash>
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

// Where an update's blocks start, by client, and whether it skips any
// (a Skip block: a gap in one client's run).
struct UpdateClocks {
    QHash<quint64, quint64> firstClock;
    bool skips = false;
};

// Empty when `update` (Yjs update v1, as y-websocket sends it) may go to
// yrs; otherwise why not, for the log. With `clocks`, also where its
// blocks start.
QString checkUpdate(const QByteArray& update, UpdateClocks* clocks = nullptr);

// The same for a state vector (sync step 1), and with `clocks` its clock
// by client.
QString checkStateVector(const QByteArray& stateVector, QHash<quint64, quint64>* clocks = nullptr);

// How much work yrs does applying an update, as the server works it out
// (web apps/server/src/ws/yjsWork.ts): one step per block each deleted
// range covers, plus the length of every block a range, or an item placed
// after or before a clock inside one, splits (yrs copies the rest of it).
// A range listed many times, or one long block split many times, costs
// far more than the update's size; neither app sends one.
// kMaxUpdateWork is twice the server's limit, so whatever it relays fits.
constexpr qint64 kMaxUpdateWork = 8'000'000;

// The work of `update` (checked with checkUpdate) on a document whose whole
// state is `docState` (empty: a new document).
qint64 updateWork(const QByteArray& docState, const QByteArray& update);

// No less than updateWork, from the document's state vector alone (no
// block is longer than its client's clocks): cheap, for deciding whether
// updateWork is needed.
qint64 updateWorkBound(const QByteArray& stateVector, const QByteArray& update);

}  // namespace bld::sync::guard
