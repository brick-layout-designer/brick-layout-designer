// Moving a placed set (a library set such as flex.group, one of your own
// sets, or a set inside a set): pressing on any of its parts and dragging
// straight away, without clicking it first, moves the whole set by the same
// amount, as in BlueBrick; undo puts every part back.

#include "core/Group.h"
#include "core/Groups.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"
#include "ui/MapViewInternal.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialog>
#include <QTimer>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>

#include <cmath>
#include <memory>

using namespace bld;
using namespace bld::ui;

namespace {

// "PL.7": a 4 x 4 stud plate with no connections.
struct PlateLibrary {
    QTemporaryDir dir;
    parts::PartsLibrary lib;
    PlateLibrary() {
        QFile f(dir.filePath(QStringLiteral("PL.7.xml")));
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<part></part>");
        f.close();
        QImage sprite(32, 32, QImage::Format_ARGB32);
        sprite.fill(Qt::darkGray);
        sprite.save(dir.filePath(QStringLiteral("PL.7.png")));
        lib.addSearchPath(dir.path());
        lib.scan();
    }
};

core::Brick plate(const QString& guid, QPointF at, const QString& group) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = QStringLiteral("PL.7");
    b.displayArea = QRectF(at, QSizeF(4, 4));
    b.myGroupId = group;
    return b;
}

core::Group group(const QString& guid, const QString& partNumber, const QString& parent = {}) {
    core::Group g;
    g.guid = guid;
    g.partNumber = partNumber;
    g.myGroupId = parent;
    return g;
}

// Three sets far apart, none picked:
//   LIB    a library set (flex.group-like): A1, A2;
//   MINE   one of your own sets ("Yard sidings"): B1, B2, B3;
//   OUTER  a set holding the set INNER (C1, C2) and the part C3.
// One loose part, L.
class SetDrag : public ::testing::Test {
protected:
    void SetUp() override {
        auto map = std::make_unique<core::Map>();
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = QStringLiteral("layer");
        layer->name = QStringLiteral("Parts");
        layer->groups.push_back(group(QStringLiteral("LIB"), QStringLiteral("FLEX.GROUP")));
        layer->groups.push_back(group(QStringLiteral("MINE"), QStringLiteral("Yard sidings")));
        layer->groups.push_back(group(QStringLiteral("OUTER"), QStringLiteral("STATION.SET")));
        layer->groups.push_back(group(QStringLiteral("INNER"), QStringLiteral("PLATFORM.SET"), QStringLiteral("OUTER")));
        layer->bricks.push_back(plate(QStringLiteral("A1"), { 0, 0 }, QStringLiteral("LIB")));
        layer->bricks.push_back(plate(QStringLiteral("A2"), { 4, 0 }, QStringLiteral("LIB")));
        layer->bricks.push_back(plate(QStringLiteral("B1"), { 0, 20 }, QStringLiteral("MINE")));
        layer->bricks.push_back(plate(QStringLiteral("B2"), { 4, 20 }, QStringLiteral("MINE")));
        layer->bricks.push_back(plate(QStringLiteral("B3"), { 4, 24 }, QStringLiteral("MINE")));
        layer->bricks.push_back(plate(QStringLiteral("C1"), { 0, 40 }, QStringLiteral("INNER")));
        layer->bricks.push_back(plate(QStringLiteral("C2"), { 4, 40 }, QStringLiteral("INNER")));
        layer->bricks.push_back(plate(QStringLiteral("C3"), { 8, 44 }, QStringLiteral("OUTER")));
        layer->bricks.push_back(plate(QStringLiteral("L"), { 30, 0 }, QString()));
        map->layers().push_back(std::move(layer));
        view_ = std::make_unique<MapView>(lib_.lib);
        view_->resize(1000, 900);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(map));
        view_->setSnapStepStuds(0);  // the grid off: the parts go where dragged
        view_->setTransform(QTransform::fromScale(kStudPx / kPx, kStudPx / kPx));
        view_->centerOn(QPointF(15 * kPx, 25 * kPx));
        for (const auto& b : bricks()) at_.insert(b.guid, b.displayArea.topLeft());
    }

    std::vector<core::Brick> bricks() const {
        return static_cast<const core::LayerBrick&>(*view_->currentMap()->layers().front()).bricks;
    }
    QPointF moved(const QString& guid) const {
        for (const auto& b : bricks())
            if (b.guid == guid) return b.displayArea.topLeft() - at_.value(guid);
        ADD_FAILURE() << "no part " << guid.toStdString();
        return {};
    }
    QPoint screen(QPointF studs) const { return view_->mapFromScene(studs * kPx); }
    // Where the pointer really is for `studs` (whole screen pixels).
    QPointF onScreen(QPointF studs) const { return view_->mapToScene(screen(studs)) / kPx; }
    QStringList picked() const {
        QStringList out;
        for (QGraphicsItem* it : view_->scene()->selectedItems())
            if (it->data(detail::kBrickDataKind).toString() == QLatin1String("brick"))
                out << it->data(detail::kBrickDataGuid).toString();
        out.sort();
        return out;
    }
    // Presses at `from` (studs) and drags straight to `to` without a click first.
    void pressAndDrag(QPointF from, QPointF to) {
        QWidget* vp = view_->viewport();
        QTest::mousePress(vp, Qt::LeftButton, {}, screen(from));
        const int steps = 12;
        for (int i = 1; i <= steps; ++i) {
            const QPoint p = screen(from + (to - from) * i / steps);
            QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
            QApplication::sendEvent(vp, &move);
        }
        QTest::mouseRelease(vp, Qt::LeftButton, {}, screen(to));
    }
    // Every part in `guids` moved by `delta`, and nothing else moved.
    void expectMovedTogether(const QStringList& guids, QPointF delta) {
        for (const auto& b : bricks()) {
            const QPointF d = moved(b.guid);
            const QPointF want = guids.contains(b.guid) ? delta : QPointF();
            EXPECT_NEAR(d.x(), want.x(), 1e-6) << b.guid.toStdString();
            EXPECT_NEAR(d.y(), want.y(), 1e-6) << b.guid.toStdString();
        }
    }
    void expectAllBack() { expectMovedTogether({}, {}); }

    static constexpr double kStudPx = 14.0;
    static constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;
    PlateLibrary lib_;
    std::unique_ptr<MapView> view_;
    QHash<QString, QPointF> at_;
};

}  // namespace

TEST_F(SetDrag, PressingAnUnpickedPartOfALibrarySetDragsTheWholeSet) {
    ASSERT_TRUE(picked().isEmpty());
    const QPointF from = onScreen({ 6, 2 });  // A2
    const QPointF to = onScreen({ 16.5, 9.25 });
    pressAndDrag(from, to);
    expectMovedTogether({ QStringLiteral("A1"), QStringLiteral("A2") }, to - from);
    EXPECT_EQ(picked(), (QStringList{ QStringLiteral("A1"), QStringLiteral("A2") }));
    view_->undoStack()->undo();
    expectAllBack();
    view_->undoStack()->redo();
    expectMovedTogether({ QStringLiteral("A1"), QStringLiteral("A2") }, to - from);
}

TEST_F(SetDrag, PressingAnUnpickedPartOfYourOwnSetDragsTheWholeSet) {
    // By its first part, then (from nothing picked again) by another.
    const QStringList set{ QStringLiteral("B1"), QStringLiteral("B2"), QStringLiteral("B3") };
    QPointF from = onScreen({ 2, 22 });  // B1
    QPointF to = onScreen({ 12.25, 30 });
    pressAndDrag(from, to);
    expectMovedTogether(set, to - from);
    view_->undoStack()->undo();
    expectAllBack();
    view_->scene()->clearSelection();
    from = onScreen({ 6, 26 });  // B3
    to = onScreen({ -3.5, 31 });
    pressAndDrag(from, to);
    expectMovedTogether(set, to - from);
    view_->undoStack()->undo();
    expectAllBack();
}

TEST_F(SetDrag, PressingAPartOfASetInsideASetDragsTheOutermostSet) {
    const QStringList all{ QStringLiteral("C1"), QStringLiteral("C2"), QStringLiteral("C3") };
    const QPointF from = onScreen({ 2, 42 });  // C1, in INNER
    const QPointF to = onScreen({ 20.5, 35 });
    pressAndDrag(from, to);
    expectMovedTogether(all, to - from);
    view_->undoStack()->undo();
    expectAllBack();
}

TEST_F(SetDrag, PressingAnUnpickedPartLeavesOtherPickedThingsBehind) {
    // The loose part is picked; a press on a set's part picks that set instead.
    for (QGraphicsItem* it : view_->scene()->items())
        it->setSelected(it->data(detail::kBrickDataGuid).toString() == QLatin1String("L"));
    const QPointF from = onScreen({ 2, 2 });  // A1
    const QPointF to = onScreen({ 10, 12 });
    pressAndDrag(from, to);
    expectMovedTogether({ QStringLiteral("A1"), QStringLiteral("A2") }, to - from);
}

// R in the middle of a drag turns the whole set, not only the grabbed
// part: every part turns by the same step and the set keeps its shape.
TEST_F(SetDrag, TurningInTheMiddleOfADragTurnsTheWholeSet) {
    const QStringList set{ QStringLiteral("B1"), QStringLiteral("B2"), QStringLiteral("B3") };
    const auto centres = [&] {
        QHash<QString, QPointF> out;
        for (const auto& b : bricks()) out.insert(b.guid, b.displayArea.center());
        return out;
    };
    const auto turns = [&] {
        QHash<QString, double> out;
        for (const auto& b : bricks()) out.insert(b.guid, b.orientation);
        return out;
    };
    const auto c0 = centres();
    const auto t0 = turns();
    QWidget* vp = view_->viewport();
    const QPointF from = onScreen({ 2, 22 });  // B1
    const QPointF mid = onScreen({ 8, 26 });
    const QPointF to = onScreen({ 14, 31 });
    QTest::mousePress(vp, Qt::LeftButton, {}, screen(from));
    for (int i = 1; i <= 6; ++i) {
        const QPoint p = screen(from + (mid - from) * i / 6);
        QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &move);
    }
    QTest::keyClick(view_.get(), Qt::Key_R);
    for (int i = 1; i <= 6; ++i) {
        const QPoint p = screen(mid + (to - mid) * i / 6);
        QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &move);
    }
    QTest::mouseRelease(vp, Qt::LeftButton, {}, screen(to));
    const auto c1 = centres();
    const auto t1 = turns();
    const double step = t1.value(QStringLiteral("B1")) - t0.value(QStringLiteral("B1"));
    EXPECT_GT(std::abs(step), 1.0) << "the set didn't turn";
    for (const QString& g : set) EXPECT_NEAR(t1.value(g) - t0.value(g), step, 1e-3) << g.toStdString();
    // Its shape: the distances between its parts are what they were.
    for (const QString& a : set)
        for (const QString& b : set)
            EXPECT_NEAR(QLineF(c1.value(a), c1.value(b)).length(), QLineF(c0.value(a), c0.value(b)).length(), 1e-3)
                << a.toStdString() << "-" << b.toStdString();
    // Nothing else moved or turned.
    for (const QString& g : { QStringLiteral("A1"), QStringLiteral("C1"), QStringLiteral("L") }) {
        EXPECT_NEAR(QLineF(c1.value(g), c0.value(g)).length(), 0.0, 1e-6) << g.toStdString();
        EXPECT_NEAR(t1.value(g), t0.value(g), 1e-6) << g.toStdString();
    }
}

// A set picked by a click moves whole with the arrow keys.
TEST_F(SetDrag, ArrowKeysMoveTheWholeSet) {
    QTest::mouseClick(view_->viewport(), Qt::LeftButton, {}, screen({ 2, 42 }));  // C1, in INNER, in OUTER
    EXPECT_EQ(picked(), (QStringList{ QStringLiteral("C1"), QStringLiteral("C2"), QStringLiteral("C3") }));
    QTest::keyClick(view_.get(), Qt::Key_Right);
    QTest::keyClick(view_.get(), Qt::Key_Down);
    const QPointF d = moved(QStringLiteral("C1"));
    EXPECT_GT(d.x(), 0.0);
    EXPECT_GT(d.y(), 0.0);
    expectMovedTogether({ QStringLiteral("C1"), QStringLiteral("C2"), QStringLiteral("C3") }, d);
}

// A duplicated set is a set of its own: pressing on the copy and dragging
// moves the whole copy and leaves the original where it was.
TEST_F(SetDrag, ADuplicatedSetDragsWholeAndLeavesTheOriginal) {
    QTest::mouseClick(view_->viewport(), Qt::LeftButton, {}, screen({ 2, 2 }));  // A1
    view_->duplicateSelection();
    const auto all = bricks();
    ASSERT_EQ(all.size(), 11u);
    QStringList copies;
    for (const auto& b : all)
        if (!at_.contains(b.guid)) copies << b.guid;
    ASSERT_EQ(copies.size(), 2);
    QHash<QString, QPointF> copyAt;
    for (const auto& b : all)
        if (copies.contains(b.guid)) copyAt.insert(b.guid, b.displayArea.topLeft());
    view_->scene()->clearSelection();
    QPointF grab;
    for (const auto& b : all)
        if (b.guid == copies.front()) grab = b.displayArea.center();
    const QPointF from = onScreen(grab);
    const QPointF to = onScreen(grab + QPointF(9.5, 13.25));
    pressAndDrag(from, to);
    for (const auto& b : bricks()) {
        if (copies.contains(b.guid)) {
            EXPECT_NEAR(b.displayArea.x() - copyAt.value(b.guid).x(), (to - from).x(), 1e-6) << "copy " << b.guid.toStdString();
            EXPECT_NEAR(b.displayArea.y() - copyAt.value(b.guid).y(), (to - from).y(), 1e-6) << "copy " << b.guid.toStdString();
        } else {
            EXPECT_NEAR(QLineF(b.displayArea.topLeft(), at_.value(b.guid)).length(), 0.0, 1e-6) << b.guid.toStdString();
        }
    }
}

// The real library's sets, with the snapping a new layout has (the grid
// and connections on): pressed on any part and dragged at once, every part
// moves by the same amount, and undo puts them back.
namespace {

class LibrarySetDrag : public ::testing::Test {
protected:
    void SetUp() override {
        parts_.addSearchPath(QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT));
        parts_.scan();
        view_ = std::make_unique<MapView>(parts_);
        view_->resize(1200, 1000);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        auto map = std::make_unique<core::Map>();
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = QStringLiteral("layer");
        layer->name = QStringLiteral("Parts");
        map->layers().push_back(std::move(layer));
        view_->loadMap(std::move(map));
        view_->setTransform(QTransform::fromScale(kStudPx / kPx, kStudPx / kPx));
        view_->setSnapStepStuds(8.0);
        view_->centerOn(QPointF(0, 0));
    }
    const core::LayerBrick& layer() const {
        return static_cast<const core::LayerBrick&>(*view_->currentMap()->layers().front());
    }
    QPoint screen(QPointF studs) const { return view_->mapFromScene(studs * kPx); }
    void pressAndDrag(QPointF from, QPointF to) {
        QWidget* vp = view_->viewport();
        QTest::mousePress(vp, Qt::LeftButton, {}, screen(from));
        const int steps = 12;
        for (int i = 1; i <= steps; ++i) {
            const QPoint p = screen(from + (to - from) * i / steps);
            QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
            QApplication::sendEvent(vp, &move);
        }
        QTest::mouseRelease(vp, Qt::LeftButton, {}, screen(to));
    }
    // Places `set`, unpicks it, then presses on each of its parts in turn and
    // drags: the whole set moves together, its shape unchanged; undo puts it back.
    void checkSet(const QString& set) {
        if (!parts_.metadata(set)) GTEST_SKIP() << "no " << set.toStdString() << " in the library";
        view_->addPartAtScenePos(set, QPointF(0, 0));
        ASSERT_GE(layer().bricks.size(), 2u) << set.toStdString();
        const auto before = layer().bricks;
        QRectF box;
        for (const auto& b : before) box |= b.displayArea;
        view_->centerOn(box.center() * kPx);
        for (std::size_t grab = 0; grab < before.size(); ++grab) {
            view_->scene()->clearSelection();
            const QPointF from = before[grab].displayArea.center();
            const QPointF to = from + QPointF(13.3, 9.7);
            ASSERT_TRUE(view_->viewport()->rect().contains(screen(to))) << set.toStdString() << " part " << grab;
            pressAndDrag(from, to);
            const auto after = layer().bricks;
            if (after.size() != before.size()) {
                std::string info;
                for (const auto& l : view_->currentMap()->layers())
                    info += l->name.toStdString() + " kind " + std::to_string(int(l->kind())) + " bricks "
                            + (l->kind() == core::LayerKind::Brick
                                   ? std::to_string(static_cast<const core::LayerBrick&>(*l).bricks.size())
                                   : std::string("-"))
                            + "; ";
                info += " undo " + std::to_string(view_->undoStack()->count()) + " at "
                        + std::to_string(view_->undoStack()->index()) + ": ";
                for (int u = 0; u < view_->undoStack()->count(); ++u)
                    info += view_->undoStack()->text(u).toStdString() + " | ";
                ADD_FAILURE() << set.toStdString() << " grabbed by " << grab << ": " << info;
            }
            ASSERT_EQ(after.size(), before.size());
            const QPointF delta = after[grab].displayArea.topLeft() - before[grab].displayArea.topLeft();
            EXPECT_GT(std::hypot(delta.x(), delta.y()), 4.0) << set.toStdString() << " grabbed by part " << grab;
            for (std::size_t i = 0; i < after.size(); ++i) {
                const QPointF d = after[i].displayArea.topLeft() - before[i].displayArea.topLeft();
                EXPECT_NEAR(d.x(), delta.x(), 1e-6) << set.toStdString() << " part " << i << " grabbed by " << grab;
                EXPECT_NEAR(d.y(), delta.y(), 1e-6) << set.toStdString() << " part " << i << " grabbed by " << grab;
                EXPECT_NEAR(after[i].orientation, before[i].orientation, 1e-6);
            }
            const int count = view_->undoStack()->count();
            const int index = view_->undoStack()->index();
            view_->undoStack()->undo();
            ASSERT_EQ(layer().bricks.size(), before.size()) << set.toStdString() << " grabbed by " << grab << ": undo stack "
                                                            << count << " at " << index << " text "
                                                            << view_->undoStack()->text(index - 1).toStdString();
            for (std::size_t i = 0; i < before.size(); ++i)
                EXPECT_NEAR((layer().bricks[i].displayArea.topLeft() - before[i].displayArea.topLeft()).manhattanLength(),
                            0.0, 1e-6);
        }
    }

    static constexpr double kStudPx = 5.0;
    static constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;
    parts::PartsLibrary parts_;
    std::unique_ptr<MapView> view_;
};

}  // namespace

TEST_F(LibrarySetDrag, ACrossoverMovesWhole) { checkSet(QStringLiteral("Single_Crossover_Left")); }
TEST_F(LibrarySetDrag, AFlexTrackMovesWhole) { checkSet(QStringLiteral("flex.group")); }
TEST_F(LibrarySetDrag, ARailYardMovesWhole) { checkSet(QStringLiteral("Rail_Yard_Left_Turn")); }

// In the whole window, as people use it: a set placed from the Parts panel,
// unpicked by a click on empty floor, then pressed on and dragged at once.
TEST(SetDragInWindow, APlacedSetDragsWholeFromAnUnpickedPart) {
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    lib.addSearchPath(QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT));
    lib.scan();
    if (!lib.metadata(QStringLiteral("Single_Crossover_Left"))) GTEST_SKIP() << "no parts library";
    // The welcome and anything else that asks on the way in: closed.
    QTimer closer;
    QObject::connect(&closer, &QTimer::timeout, [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
    });
    closer.start(20);
    {
        ui::MainWindow window(lib);
        window.resize(1400, 1000);
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        auto* view = window.findChild<MapView*>();
        ASSERT_TRUE(view);
        {
            auto map = std::make_unique<core::Map>();
            auto layer = std::make_unique<core::LayerBrick>();
            layer->guid = QStringLiteral("layer");
            layer->name = QStringLiteral("Parts");
            map->layers().push_back(std::move(layer));
            view->loadMap(std::move(map));
        }
        ASSERT_TRUE(view->currentMap());
        view->addPartAtScenePos(QStringLiteral("Single_Crossover_Left"), QPointF(0, 0));
        const auto layerOf = [&]() -> const core::LayerBrick* {
            for (const auto& l : view->currentMap()->layers())
                if (l->kind() == core::LayerKind::Brick && !static_cast<const core::LayerBrick&>(*l).bricks.empty())
                    return static_cast<const core::LayerBrick*>(l.get());
            return nullptr;
        };
        ASSERT_TRUE(layerOf());
        const auto before = layerOf()->bricks;
        ASSERT_GE(before.size(), 2u);
        QRectF box;
        for (const auto& b : before) box |= b.displayArea;
        constexpr double px = rendering::SceneBuilder::kPixelsPerStud;
        view->setTransform(QTransform::fromScale(5.0 / px, 5.0 / px));
        view->centerOn(box.center() * px);
        const auto screen = [&](QPointF studs) { return view->mapFromScene(studs * px); };
        QWidget* vp = view->viewport();
        // Unpicked by a click on the floor beside it.
        QTest::mouseClick(vp, Qt::LeftButton, {}, screen(box.bottomRight() + QPointF(20, 20)));
        ASSERT_TRUE(view->scene()->selectedItems().isEmpty());
        const QPointF from = before.back().displayArea.center();
        const QPointF to = from + QPointF(13.3, 9.7);
        QTest::mousePress(vp, Qt::LeftButton, {}, screen(from));
        for (int i = 1; i <= 12; ++i) {
            const QPoint p = screen(from + (to - from) * i / 12);
            QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
            QApplication::sendEvent(vp, &move);
        }
        QTest::mouseRelease(vp, Qt::LeftButton, {}, screen(to));
        const auto after = layerOf()->bricks;
        ASSERT_EQ(after.size(), before.size());
        const QPointF delta = after.back().displayArea.topLeft() - before.back().displayArea.topLeft();
        EXPECT_GT(std::hypot(delta.x(), delta.y()), 4.0);
        for (std::size_t i = 0; i < after.size(); ++i) {
            EXPECT_NEAR(after[i].displayArea.x() - before[i].displayArea.x(), delta.x(), 1e-6) << "part " << i;
            EXPECT_NEAR(after[i].displayArea.y() - before[i].displayArea.y(), delta.y(), 1e-6) << "part " << i;
        }
        view->undoStack()->undo();
        for (std::size_t i = 0; i < before.size(); ++i)
            EXPECT_NEAR((layerOf()->bricks[i].displayArea.topLeft() - before[i].displayArea.topLeft()).manhattanLength(), 0.0, 1e-6);
        window.close();
    }
    QStandardPaths::setTestModeEnabled(false);
}
