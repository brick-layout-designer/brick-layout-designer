#pragma once

// A live shared layout document (yrs), as the desktop holds it while
// connected to a collaborative server. Remote updates are applied with
// applyUpdate; local edits go in with writeMap, which turns the desktop's
// layout into the smallest change to the document, matched by item id,
// and returns it as the update to send. Sync phase P3b.

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <memory>

namespace bld::core { class Map; }

namespace bld::sync {

class SyncDoc {
public:
    SyncDoc();
    // For tests only: a doc with this client id (the next one after a
    // reset), so a copy of it makes the same writes; with `keepDeleted`,
    // yrs keeps deleted content instead of collecting it.
    explicit SyncDoc(quint64 clientId, bool keepDeleted = false);
    ~SyncDoc();
    SyncDoc(const SyncDoc&) = delete;
    SyncDoc& operator=(const SyncDoc&) = delete;

    // Apply a Yjs v1 update (the server's state, or a peer's change). One
    // that fails UpdateGuard's checks is refused before yrs reads it.
    bool applyUpdate(const QByteArray& update, QString* error = nullptr);
    // Forget everything: an empty document, as when made. Before a
    // different layout is read into it, so the two don't merge.
    void reset();

    // The whole document as a v1 update, and its state vector.
    QByteArray encodeState() const;
    QByteArray stateVector() const;
    // What a peer with state vector `sv` is missing, as a v1 update (sync
    // step 2). An unreadable state vector gets the whole document.
    QByteArray diffSince(const QByteArray& sv) const;

    // Root types as JSON (see DocJson.h). A copy is kept between calls and
    // changed by what each transaction changed (yrs deep observers), so on a
    // big layout this costs nothing; only when the copy can't follow a
    // change is the whole document read again.
    QJsonObject toJson() const;
    // The whole document read again from yrs, past the kept copy: what
    // toJson must equal (tests).
    QJsonObject readWhole() const;
    // For tests: false reads the whole document on every toJson instead,
    // as before the kept copy.
    void setKeepJson(bool keep);
    // How many times toJson has read the whole document (the perf tests
    // count it).
    static int wholeReads();

    // Make the document hold `map`, changing only what differs: header
    // fields, the layer order, whole layers added or removed, and within a
    // layer the bricks, groups and text cells by id (fields updated in
    // place, items inserted or deleted). Areas and rulers, stored as plain
    // lists, are replaced when they differ. Keys the desktop doesn't know
    // are left alone. Returns the change as a v1 update, empty when the
    // document already matched.
    QByteArray writeMap(const core::Map& map);
    // Set one meta field (as another writer would, e.g. a newer app stamping
    // meta.schemaVersion); not an undoable step. Returns the update to send.
    QByteArray setMeta(const QString& key, const QJsonValue& value);

    // Undo / redo this desktop's own writeMap changes only: other people's
    // edits (applied with applyUpdate) are never undone, and undoing a
    // change keeps what others did since. Each writeMap is one step. The
    // result is the update to send, empty when there was nothing to undo.
    QByteArray undo();
    QByteArray redo();
    bool canUndo() const;
    bool canRedo() const;

    // How much array work writeMap has done so far (the perf tests count
    // it): entries read by index, each of which walks the array from its
    // start, insert calls, and entries compared field by field with the
    // layout (parts nobody changed since the last write aren't).
    struct ArrayWork {
        int readsByIndex = 0;
        int inserts = 0;
        int entriesCompared = 0;
    };
    static ArrayWork arrayWork();

private:
    void init();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace bld::sync
