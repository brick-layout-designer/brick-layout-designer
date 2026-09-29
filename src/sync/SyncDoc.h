#pragma once

// A live shared layout document (yrs), as the desktop holds it while
// connected to a collaborative server. Remote updates are applied with
// applyUpdate; local edits go in with writeMap, which turns the desktop's
// layout into the smallest change to the document, matched by item id,
// and returns it as the update to send. Sync phase P3b.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <memory>

namespace bld::core { class Map; }

namespace bld::sync {

class SyncDoc {
public:
    SyncDoc();
    ~SyncDoc();
    SyncDoc(const SyncDoc&) = delete;
    SyncDoc& operator=(const SyncDoc&) = delete;

    // Apply a Yjs v1 update (the server's state, or a peer's change).
    bool applyUpdate(const QByteArray& update, QString* error = nullptr);

    // The whole document as a v1 update, and its state vector.
    QByteArray encodeState() const;
    QByteArray stateVector() const;
    // What a peer with state vector `sv` is missing, as a v1 update (sync
    // step 2). An unreadable state vector gets the whole document.
    QByteArray diffSince(const QByteArray& sv) const;

    // Root types as JSON (see DocJson.h).
    QJsonObject toJson() const;

    // Make the document hold `map`, changing only what differs: header
    // fields, the layer order, whole layers added or removed, and within a
    // layer the bricks, groups and text cells by id (fields updated in
    // place, items inserted or deleted). Areas and rulers, stored as plain
    // lists, are replaced when they differ. Keys the desktop doesn't know
    // are left alone. Returns the change as a v1 update, empty when the
    // document already matched.
    QByteArray writeMap(const core::Map& map);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace bld::sync
