// SyncDoc keeps the document's JSON between writes and changes it by what
// each transaction changed, instead of reading the whole document again
// (SyncDoc.h). These tests hold it to the old way:
//
// - Random edit sequences, with other people's edits arriving between ours
//   (one at a time, out of order, or bundled as after a reconnect), undo,
//   redo, a ruler sheet turned into Y.Maps (doc shape 2), drawing settings
//   out of range, sidecar items and starting again from the server's copy:
//   the kept JSON must equal a whole read after every step, and each write
//   must do what the same write does on a copy that reads the document
//   whole (every clock the same, byte for byte the same JSON after).
// - In steady state, an edit reads the whole document no times at all.

#include "SyncDoc.h"
#include "WebModel.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <random>

using namespace bld;

namespace {

QByteArray webDoc() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/layers.ydoc"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray bytes(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

// A state vector, client → clock. yrs writes its entries in hash order, and
// JSON maps in an update too, so two docs that did the same are compared by
// what they hold, not by the bytes.
std::map<quint64, quint64> clocks(const QByteArray& sv) {
    std::map<quint64, quint64> out;
    qsizetype at = 0;
    const auto next = [&] {
        quint64 v = 0;
        for (int shift = 0; at < sv.size(); shift += 7) {
            const auto b = static_cast<quint8>(sv[at++]);
            v |= static_cast<quint64>(b & 0x7f) << shift;
            if (!(b & 0x80)) break;
        }
        return v;
    };
    for (quint64 n = next(); n > 0; --n) {
        const quint64 client = next();
        out[client] = next();
    }
    return out;
}

std::unique_ptr<core::Map> mapOf(const sync::SyncDoc& doc) {
    QString err;
    auto map = sync::mapFromDocJson(doc.toJson(), &err);
    EXPECT_TRUE(map) << err.toStdString();
    return map;
}

template <typename T>
T* firstOf(core::Map& map, core::LayerKind kind) {
    for (auto& l : map.layers())
        if (l->kind() == kind) return static_cast<T*>(l.get());
    return nullptr;
}

// Random edits of a map, as the editor makes them.
class Editor {
public:
    Editor(std::mt19937& rng, int idBase, bool othersDelete) : rng_(rng), next_(idBase), othersDelete_(othersDelete) {}

    void edit(core::Map& m, bool wild) {
        int kind = pick(16);
        if (wild && !othersDelete_ && (kind == 5 || kind == 8)) kind = 0;  // (see randomEdits)
        switch (kind) {
        case 0: case 1: case 2: {  // move a part
            if (auto* b = someBrick(m)) b->displayArea.translate(pick(9) - 4, pick(9) - 4);
            break;
        }
        case 3: {  // turn a part, or lift it
            if (auto* b = someBrick(m)) {
                if (pick(2)) b->orientation = static_cast<float>(pick(8) * 45);
                else b->altitude = static_cast<float>(pick(5));
            }
            break;
        }
        case 4: {  // add parts: a few, or a paste
            auto& l = brickLayer(m);
            const int n = pick(6) == 0 ? 10 + pick(20) : 1 + pick(3);
            const auto at = static_cast<std::ptrdiff_t>(pick(2) ? l.bricks.size() : pick(static_cast<int>(l.bricks.size()) + 1));
            std::vector<core::Brick> added;
            for (int i = 0; i < n; ++i) added.push_back(newBrick(l));
            l.bricks.insert(l.bricks.begin() + at, added.begin(), added.end());
            break;
        }
        case 5: {  // delete parts
            auto& l = brickLayer(m);
            for (int n = 1 + pick(4); n > 0 && !l.bricks.empty(); --n)
                l.bricks.erase(l.bricks.begin() + pick(static_cast<int>(l.bricks.size())));
            break;
        }
        case 6: {  // bring to front, send to back (doc shape 2 sets z)
            auto& l = brickLayer(m);
            if (l.bricks.size() < 2) break;
            const auto i = static_cast<std::ptrdiff_t>(pick(static_cast<int>(l.bricks.size())));
            core::Brick b = l.bricks[static_cast<size_t>(i)];
            l.bricks.erase(l.bricks.begin() + i);
            if (pick(2)) l.bricks.push_back(b);
            else l.bricks.insert(l.bricks.begin(), b);
            break;
        }
        case 7: {  // a sheet's own settings, out of range from another writer
            if (m.layers().empty()) break;
            auto& l = *m.layers()[static_cast<size_t>(pick(static_cast<int>(m.layers().size())))];
            switch (pick(3)) {
            case 0: l.name = QStringLiteral("Sheet %1").arg(next_++); break;
            case 1: l.visible = !l.visible; break;
            default: l.transparency = wild ? 400 - pick(800) : pick(101); break;
            }
            if (auto* g = firstOf<core::LayerGrid>(m, core::LayerKind::Grid); g && wild && pick(2)) {
                g->gridThickness = 5000.0f;
                g->subDivisionNumber = 1;
                g->cellIndexFont.sizePt = 9000.0f;
            }
            break;
        }
        case 8: {  // add a sheet, or take one away
            if (pick(2) && m.layers().size() > 1) {
                m.layers().erase(m.layers().begin() + pick(static_cast<int>(m.layers().size())));
            } else if (pick(2)) {
                auto l = std::make_unique<core::LayerBrick>();
                l->guid = id();
                l->name = QStringLiteral("Parts %1").arg(l->guid);
                for (int i = pick(4); i > 0; --i) l->bricks.push_back(newBrick(*l));
                m.layers().push_back(std::move(l));
            } else {
                auto l = std::make_unique<core::LayerText>();
                l->guid = id();
                l->name = QStringLiteral("Notes");
                m.layers().push_back(std::move(l));
            }
            break;
        }
        case 9: {  // sheet order
            if (m.layers().size() > 1) std::swap(m.layers()[0], m.layers()[static_cast<size_t>(1 + pick(static_cast<int>(m.layers().size()) - 1))]);
            break;
        }
        case 10: {  // text
            auto* t = firstOf<core::LayerText>(m, core::LayerKind::Text);
            if (!t) break;
            if (t->textCells.empty() || pick(3) == 0) {
                core::TextCell c;
                c.guid = id();
                c.text = QStringLiteral("Note %1").arg(c.guid);
                c.displayArea = QRectF(pick(100), pick(100), 20, 8);
                t->textCells.push_back(c);
            } else {
                auto& c = t->textCells[static_cast<size_t>(pick(static_cast<int>(t->textCells.size())))];
                if (pick(2)) c.text += QStringLiteral("!");
                else c.displayArea.translate(1, 1);
            }
            break;
        }
        case 11: {  // area cells: paint one, or move the whole sheet (doc shape 2 sets cellOffset)
            auto* a = firstOf<core::LayerArea>(m, core::LayerKind::Area);
            if (!a) break;
            if (a->cells.empty() || pick(2)) {
                a->cells.push_back({ pick(30), pick(30), QColor::fromRgba(0xff000000u | static_cast<quint32>(pick(0xffffff))) });
            } else {
                for (auto& c : a->cells) c.x += 1;
            }
            break;
        }
        case 12: {  // rulers: add, move, reorder (doc shape 2 makes them Y.Maps)
            auto* r = firstOf<core::LayerRuler>(m, core::LayerKind::Ruler);
            if (!r) break;
            if (r->rulers.empty() || pick(3) == 0) {
                core::LayerRuler::AnyRuler any;
                any.linear.guid = id();
                any.linear.point1 = QPointF(pick(50), pick(50));
                any.linear.point2 = QPointF(pick(50) + 60, pick(50));
                any.linear.displayArea = QRectF(any.linear.point1, any.linear.point2).normalized();
                r->rulers.push_back(any);
            } else if (pick(2)) {
                auto& any = r->rulers[static_cast<size_t>(pick(static_cast<int>(r->rulers.size())))];
                any.linear.offsetDistance += 2;
                any.circular.radius += 1;
            } else if (r->rulers.size() > 1) {
                std::swap(r->rulers.front(), r->rulers.back());
            }
            break;
        }
        case 13: {  // group two parts
            auto& l = brickLayer(m);
            if (l.bricks.size() < 2) break;
            core::Group g;
            g.guid = id();
            l.groups.push_back(g);
            l.bricks[static_cast<size_t>(pick(static_cast<int>(l.bricks.size())))].myGroupId = g.guid;
            l.bricks[static_cast<size_t>(pick(static_cast<int>(l.bricks.size())))].myGroupId = g.guid;
            break;
        }
        case 14: {  // sidecar: labels and modules (meta.cache and sidecarItems)
            auto& sc = m.sidecar;
            if (pick(2)) {
                if (!sc.anchoredLabels.empty() && pick(2)) sc.anchoredLabels.erase(sc.anchoredLabels.begin());
                else {
                    core::AnchoredLabel label;
                    label.id = id();
                    label.text = QStringLiteral("Label %1").arg(label.id);
                    label.offset = QPointF(pick(40), pick(40));
                    sc.anchoredLabels.push_back(label);
                }
            } else {
                if (!sc.modules.empty() && pick(2)) {
                    sc.modules.back().name += QStringLiteral("+");
                } else {
                    core::Module mod;
                    mod.id = id();
                    mod.name = QStringLiteral("Module %1").arg(mod.id);
                    if (auto* b = someBrick(m)) mod.memberIds.insert(b->guid);
                    sc.modules.push_back(mod);
                }
            }
            break;
        }
        default: {  // the header
            m.comment = QStringLiteral("Edited %1").arg(next_++);
            break;
        }
        }
    }

private:
    int pick(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(rng_); }
    QString id() { return QString::number(next_++); }

    core::LayerBrick& brickLayer(core::Map& m) {
        if (auto* l = firstOf<core::LayerBrick>(m, core::LayerKind::Brick)) return *l;
        auto l = std::make_unique<core::LayerBrick>();
        l->guid = id();
        auto* out = l.get();
        m.layers().push_back(std::move(l));
        return *out;
    }

    core::Brick* someBrick(core::Map& m) {
        auto& l = brickLayer(m);
        return l.bricks.empty() ? nullptr : &l.bricks[static_cast<size_t>(pick(static_cast<int>(l.bricks.size())))];
    }

    core::Brick newBrick(const core::LayerBrick& l) {
        core::Brick b;
        if (!l.bricks.empty()) b = l.bricks[static_cast<size_t>(pick(static_cast<int>(l.bricks.size())))];
        else {
            b.partNumber = QStringLiteral("3001.8");
            b.displayArea = QRectF(0, 0, 4, 2);
            b.connections.resize(2);
        }
        b.guid = id();
        b.myGroupId.clear();
        for (auto& c : b.connections) {
            c.guid = id();
            c.linkedToId.clear();
        }
        b.displayArea.moveTo(pick(400), pick(400));
        return b;
    }

    std::mt19937& rng_;
    int next_;
    bool othersDelete_;
};

// Someone else on the same layout, with their own copy of the document.
struct Peer {
    std::unique_ptr<sync::SyncDoc> doc;
    std::unique_ptr<core::Map> map;
    std::deque<QByteArray> inbox;
};

}  // namespace

// One run of random steps per seed (see the top of the file). With
// `othersDelete`, the others delete parts and sheets too, while our undo
// and redo bring back what we changed. Every doc is made as the app makes
// it (deleted content kept).
static void randomEdits(bool othersDelete) {
    const QByteArray base = webDoc();
    ASSERT_FALSE(base.isEmpty());
    // (BLD_SEED_FROM / BLD_SEED_TO run other seeds, e.g. a few hundred.)
    const int seedFrom = qEnvironmentVariableIntValue("BLD_SEED_FROM") > 0 ? qEnvironmentVariableIntValue("BLD_SEED_FROM") : 1;
    const int seedTo = qEnvironmentVariableIntValue("BLD_SEED_TO") > 0 ? qEnvironmentVariableIntValue("BLD_SEED_TO") : 8;
    for (int seed = seedFrom; seed <= seedTo; ++seed) {
        SCOPED_TRACE(::testing::Message() << "seed " << seed);
        std::mt19937 rng(static_cast<unsigned>(seed));
        const auto pick = [&rng](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
        Editor editor(rng, 500000 + seed * 100000, othersDelete);

        sync::SyncDoc server(1);
        ASSERT_TRUE(server.applyUpdate(base));
        quint64 mineId = 1000;
        auto mine = std::make_unique<sync::SyncDoc>(mineId);
        ASSERT_TRUE(mine->applyUpdate(base));
        auto map = mapOf(*mine);
        std::deque<QByteArray> inbox;

        // The web's layout, cut down to a few dozen parts a sheet so the
        // steps stay quick.
        for (auto& l : map->layers())
            if (l->kind() == core::LayerKind::Brick) {
                auto& bricks = static_cast<core::LayerBrick&>(*l).bricks;
                if (bricks.size() > 40) bricks.resize(40);
            }
        const QByteArray cut = mine->writeMap(*map);
        ASSERT_TRUE(server.applyUpdate(cut));

        std::vector<Peer> peers(2);
        for (size_t p = 0; p < peers.size(); ++p) {
            peers[p].doc = std::make_unique<sync::SyncDoc>(2000 + p);
            ASSERT_TRUE(peers[p].doc->applyUpdate(base));
            ASSERT_TRUE(peers[p].doc->applyUpdate(cut));
            peers[p].map = mapOf(*peers[p].doc);
        }
        const auto sendFromMine = [&](const QByteArray& u) {
            if (u.isEmpty()) return;
            ASSERT_TRUE(server.applyUpdate(u));
            for (auto& p : peers) p.inbox.push_back(u);
        };

        for (int step = 0; step < 60; ++step) {
            SCOPED_TRACE(::testing::Message() << "step " << step);
            const int what = pick(100);
            if (what < 32) {  // an edit of mine
                editor.edit(*map, false);
                // The same write the way it was made before: a copy of the
                // document as it is now, with the same client id, read
                // whole. (A copy, not a twin kept alongside: yrs's undo
                // brings back deleted entries in hash order, so two docs
                // that undo the same steps can hold them at different ids.)
                const QByteArray before = mine->encodeState();
                sync::SyncDoc ref(mineId), check(mineId);
                ref.setKeepJson(false);
                ASSERT_TRUE(ref.applyUpdate(before));
                ASSERT_TRUE(check.applyUpdate(before));
                const QByteArray a = mine->writeMap(*map);
                const QByteArray b = ref.writeMap(*map);
                ASSERT_EQ(a.isEmpty(), b.isEmpty());
                ASSERT_EQ(clocks(mine->stateVector()), clocks(ref.stateVector()));
                const QJsonObject want = ref.toJson();
                ASSERT_TRUE(mine->toJson() == want);
                // What it sends does the same to another copy.
                if (!a.isEmpty()) {
                    ASSERT_TRUE(check.applyUpdate(a));
                }
                ASSERT_TRUE(check.readWhole() == want);
                sendFromMine(a);
            } else if (what < 52) {  // someone else's edit
                Peer& p = peers[static_cast<size_t>(pick(static_cast<int>(peers.size())))];
                editor.edit(*p.map, true);
                const QByteArray u = p.doc->writeMap(*p.map);
                if (!u.isEmpty()) {
                    ASSERT_TRUE(server.applyUpdate(u));
                    inbox.push_back(u);
                    for (auto& other : peers)
                        if (&other != &p) other.inbox.push_back(u);
                }
            } else if (what < 66) {  // theirs arrive one at a time, now and then out of order
                if (inbox.empty()) continue;
                const size_t i = inbox.size() > 1 && pick(4) == 0 ? 1 : 0;
                const QByteArray u = inbox[i];
                inbox.erase(inbox.begin() + static_cast<std::ptrdiff_t>(i));
                ASSERT_TRUE(mine->applyUpdate(u));
                map = mapOf(*mine);
            } else if (what < 74) {  // back in step after a reconnect: all we lack at once
                ASSERT_TRUE(mine->applyUpdate(server.diffSince(mine->stateVector())));
                inbox.clear();
                map = mapOf(*mine);
            } else if (what < 82) {  // the others catch up
                for (auto& p : peers) {
                    for (const QByteArray& u : p.inbox) ASSERT_TRUE(p.doc->applyUpdate(u));
                    p.inbox.clear();
                    p.map = mapOf(*p.doc);
                }
            } else if (what < 96) {  // undo, redo
                sendFromMine(pick(3) != 0 ? mine->undo() : mine->redo());
                map = mapOf(*mine);
            } else if (what < 98) {  // a newer app stamps the shape
                sendFromMine(mine->setMeta(QStringLiteral("schemaVersion"), 2));
            } else {  // starting again from the server's copy (access changed)
                mine->reset();
                ++mineId;  // (reset moves a test doc's client id on)
                ASSERT_TRUE(mine->applyUpdate(server.encodeState()));
                inbox.clear();
                map = mapOf(*mine);
            }
            // (Compared as JSON values each step, as bytes at the end.)
            ASSERT_TRUE(mine->toJson() == mine->readWhole());
        }
        // In step at the end, it reads what the server holds.
        ASSERT_TRUE(mine->applyUpdate(server.diffSince(mine->stateVector())));
        ASSERT_EQ(bytes(mine->toJson()), bytes(mine->readWhole()));
        ASSERT_EQ(bytes(mine->toJson()), bytes(server.readWhole()));
    }
}

TEST(SyncDocKeptJson, RandomEditsWriteWhatReadingTheWholeDocumentWrites) { randomEdits(false); }

TEST(SyncDocKeptJson, RandomEditsWithEveryoneDeletingToo) { randomEdits(true); }

TEST(SyncDocKeptJson, ResetStartsAgainFromAnEmptyDocument) {
    sync::SyncDoc doc;
    ASSERT_TRUE(doc.applyUpdate(webDoc()));
    ASSERT_FALSE(doc.toJson().value(QLatin1String("layers")).toArray().isEmpty());
    doc.reset();
    EXPECT_EQ(bytes(doc.toJson()), bytes(doc.readWhole()));
    EXPECT_TRUE(doc.toJson().value(QLatin1String("layers")).toArray().isEmpty());
    ASSERT_TRUE(doc.applyUpdate(webDoc()));
    EXPECT_EQ(bytes(doc.toJson()), bytes(doc.readWhole()));
}

TEST(SyncDocKeptJson, EditsAndOtherPeoplesChangesReadNothingWhole) {
    sync::SyncDoc doc, peer;
    ASSERT_TRUE(doc.applyUpdate(webDoc()));
    ASSERT_TRUE(peer.applyUpdate(webDoc()));
    auto map = mapOf(doc);
    auto theirs = mapOf(peer);
    auto* bricks = firstOf<core::LayerBrick>(*map, core::LayerKind::Brick);
    ASSERT_TRUE(bricks && !bricks->bricks.empty());
    const int before = sync::SyncDoc::wholeReads();

    // Moving a part, undoing it, redoing it.
    bricks->bricks.front().displayArea.translate(8, 0);
    const QByteArray moved = doc.writeMap(*map);
    ASSERT_FALSE(moved.isEmpty());
    ASSERT_FALSE(doc.undo().isEmpty());
    ASSERT_FALSE(doc.redo().isEmpty());
    // Someone else's paste arriving, and the layout read for the map.
    auto* their = firstOf<core::LayerBrick>(*theirs, core::LayerKind::Brick);
    for (int i = 0; i < 30; ++i) {
        core::Brick b = their->bricks.front();
        b.guid = QStringLiteral("p%1").arg(i);
        for (auto& c : b.connections) c.guid += b.guid;
        their->bricks.push_back(b);
    }
    ASSERT_TRUE(doc.applyUpdate(peer.writeMap(*theirs)));
    map = mapOf(doc);
    EXPECT_EQ(firstOf<core::LayerBrick>(*map, core::LayerKind::Brick)->bricks.size(), their->bricks.size());

    EXPECT_EQ(sync::SyncDoc::wholeReads() - before, 0);
    EXPECT_EQ(bytes(doc.toJson()), bytes(doc.readWhole()));
}

// Someone moves a part and then deletes it, adds parts to a sheet and then
// deletes the sheet; we get both at once (back in step after a reconnect).
// The changes inside what was deleted are out of sight, and the kept JSON
// follows without reading the whole document.
TEST(SyncDocKeptJson, ChangesInsideWhatWasDeletedInTheSameUpdateAreLeftOut) {
    sync::SyncDoc doc, peer, server;
    for (auto* d : { &doc, &peer, &server }) ASSERT_TRUE(d->applyUpdate(webDoc()));
    auto theirs = mapOf(peer);
    auto* bricks = firstOf<core::LayerBrick>(*theirs, core::LayerKind::Brick);
    ASSERT_TRUE(bricks && bricks->bricks.size() > 3);
    const auto send = [&](const QByteArray& u) { ASSERT_TRUE(server.applyUpdate(u)); };
    bricks->bricks[2].displayArea.translate(16, 0);
    send(peer.writeMap(*theirs));
    bricks->bricks.erase(bricks->bricks.begin() + 2);
    send(peer.writeMap(*theirs));
    core::Brick extra = bricks->bricks.front();
    extra.guid = QStringLiteral("gone-with-the-sheet");
    bricks->bricks.push_back(extra);
    bricks->name = QStringLiteral("Renamed, then deleted");
    send(peer.writeMap(*theirs));
    const QString sheet = bricks->guid;
    auto& layers = theirs->layers();
    layers.erase(std::find_if(layers.begin(), layers.end(), [&](const auto& l) { return l->guid == sheet; }));
    send(peer.writeMap(*theirs));

    const int before = sync::SyncDoc::wholeReads();
    ASSERT_TRUE(doc.applyUpdate(server.diffSince(doc.stateVector())));
    EXPECT_EQ(sync::SyncDoc::wholeReads() - before, 0);
    EXPECT_EQ(bytes(doc.toJson()), bytes(doc.readWhole()));
    EXPECT_EQ(bytes(doc.toJson()), bytes(server.readWhole()));
}

// On a big layout an edit compares only the parts that changed since the
// last write, here or by someone else, with the document.
TEST(SyncDocKeptJson, AnEditComparesOnlyThePartsThatChanged) {
    sync::SyncDoc doc, peer;
    ASSERT_TRUE(doc.applyUpdate(webDoc()));
    auto map = mapOf(doc);
    auto* layer = firstOf<core::LayerBrick>(*map, core::LayerKind::Brick);
    ASSERT_TRUE(layer && !layer->bricks.empty());
    const core::Brick model = layer->bricks.front();
    for (int i = 0; i < 1500; ++i) {
        core::Brick b = model;
        b.guid = QStringLiteral("big%1").arg(i);
        b.myGroupId.clear();
        for (auto& c : b.connections) {
            c.guid = b.guid + c.guid;
            c.linkedToId.clear();
        }
        b.displayArea.translate(8.0 * (i % 50), 8.0 * (i / 50 + 1));
        layer->bricks.push_back(b);
    }
    ASSERT_FALSE(doc.writeMap(*map).isEmpty());
    ASSERT_TRUE(peer.applyUpdate(doc.encodeState()));
    const auto compared = [&](const std::function<void()>& edit) {
        const int before = sync::SyncDoc::arrayWork().entriesCompared;
        edit();
        return sync::SyncDoc::arrayWork().entriesCompared - before;
    };

    // Nothing changed: no part (the few text cells and rulers are always
    // compared, so this is what to count from).
    const int none = compared([&] { ASSERT_TRUE(doc.writeMap(*map).isEmpty()); });
    EXPECT_LE(none, 12);
    // One part moved: that one.
    EXPECT_EQ(compared([&] {
        layer->bricks[700].displayArea.translate(8, 0);
        ASSERT_FALSE(doc.writeMap(*map).isEmpty());
    }), none + 1);
    // Someone else moves a part; then one here: those two.
    auto theirs = mapOf(peer);
    firstOf<core::LayerBrick>(*theirs, core::LayerKind::Brick)->bricks[10].displayArea.translate(0, 8);
    ASSERT_TRUE(doc.applyUpdate(peer.writeMap(*theirs)));
    map = mapOf(doc);
    layer = firstOf<core::LayerBrick>(*map, core::LayerKind::Brick);
    EXPECT_EQ(compared([&] {
        layer->bricks[20].displayArea.translate(8, 0);
        ASSERT_FALSE(doc.writeMap(*map).isEmpty());
    }), none + 2);
    // Undone: the next edit compares the part the undo put back too.
    ASSERT_FALSE(doc.undo().isEmpty());
    map = mapOf(doc);
    layer = firstOf<core::LayerBrick>(*map, core::LayerKind::Brick);
    EXPECT_EQ(compared([&] {
        layer->bricks[30].displayArea.translate(8, 0);
        ASSERT_FALSE(doc.writeMap(*map).isEmpty());
    }), none + 2);
    EXPECT_EQ(bytes(doc.toJson()), bytes(doc.readWhole()));
}

// Someone's edits arriving out of order: the later one waits until the one
// before it is in (as Yjs keeps them pending). yrs would show it at once,
// past its state vector, and then never say when it changes, so the kept
// JSON couldn't follow.
TEST(SyncDocKeptJson, AnUpdateArrivingBeforeTheOneItFollowsWaitsForIt) {
    sync::SyncDoc doc, peer;
    ASSERT_TRUE(doc.applyUpdate(webDoc()));
    ASSERT_TRUE(peer.applyUpdate(webDoc()));
    auto theirs = mapOf(peer);
    auto* bricks = firstOf<core::LayerBrick>(*theirs, core::LayerKind::Brick);
    ASSERT_TRUE(bricks && !bricks->bricks.empty());
    const auto count = [](const QJsonObject& j) {
        qsizetype n = 0;
        for (const auto& l : j.value(QLatin1String("layerData")).toObject())
            n += l.toObject().value(QLatin1String("bricks")).toArray().size();
        return n;
    };
    const qsizetype before = count(doc.toJson());
    bricks->bricks.front().displayArea.translate(8, 0);
    const QByteArray first = peer.writeMap(*theirs);
    for (int i = 0; i < 3; ++i) {
        core::Brick b = bricks->bricks.front();
        b.guid = QStringLiteral("late%1").arg(i);
        for (auto& c : b.connections) c.guid += b.guid;
        bricks->bricks.push_back(b);
    }
    const QByteArray second = peer.writeMap(*theirs);
    bricks->bricks.back().displayArea.translate(8, 0);
    const QByteArray third = peer.writeMap(*theirs);

    ASSERT_TRUE(doc.applyUpdate(second));
    EXPECT_EQ(count(doc.toJson()), before) << "the second edit waits for the first";
    ASSERT_TRUE(doc.applyUpdate(third));
    ASSERT_TRUE(doc.applyUpdate(first));
    EXPECT_EQ(bytes(doc.toJson()), bytes(doc.readWhole()));
    EXPECT_EQ(bytes(doc.toJson()), bytes(peer.readWhole()));
}

// A live doc keeps what was deleted, rather than collecting it, so undo
// steps stay whole while others delete around them; and it does the same
// when loaded again (from the cache file or the server's copy) or after a
// reset (back to the server's copy).
TEST(SyncDocKeptJson, LiveDocsKeepWhatWasDeleted) {
    sync::SyncDoc doc;
    ASSERT_TRUE(doc.applyUpdate(webDoc()));
    auto map = mapOf(doc);
    auto* bricks = firstOf<core::LayerBrick>(*map, core::LayerKind::Brick);
    ASSERT_TRUE(bricks && !bricks->bricks.empty());
    core::Brick b = bricks->bricks.front();
    b.guid = QStringLiteral("kept-after-delete");
    b.partNumber = QStringLiteral("KEPT-AFTER-DELETE.8");
    for (auto& c : b.connections) c.guid += b.guid;
    bricks->bricks.push_back(b);
    ASSERT_FALSE(doc.writeMap(*map).isEmpty());
    bricks->bricks.pop_back();
    ASSERT_FALSE(doc.writeMap(*map).isEmpty());
    const QByteArray mark = QByteArrayLiteral("KEPT-AFTER-DELETE.8");
    EXPECT_TRUE(doc.encodeState().contains(mark)) << "the deleted part's content was collected";

    const QByteArray state = doc.encodeState();
    sync::SyncDoc loaded;
    ASSERT_TRUE(loaded.applyUpdate(state));
    EXPECT_TRUE(loaded.encodeState().contains(mark)) << "collected on loading";
    doc.reset();
    ASSERT_TRUE(doc.applyUpdate(state));
    EXPECT_TRUE(doc.encodeState().contains(mark)) << "collected after a reset";
    EXPECT_FALSE(mapOf(doc)->layers().empty());
}
