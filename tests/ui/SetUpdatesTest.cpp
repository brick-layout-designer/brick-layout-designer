// Placed custom sets keep track of their set's version (edit/SetUpdates.h):
// placing one records it in the sidecar (never the .bbm); once the set
// changes, "Update available" shows over the map and in the set's
// right-click menu; Update puts the new parts where the set sits now (moved
// and turned since), keeping its group and a module it's in, in one undo
// step; a duplicate keeps track too.

#include "edit/SetUpdates.h"
#include "core/Groups.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/EditCommands.h"
#include "edit/Sets.h"
#include "parts/PartsLibrary.h"
#include "parts/SetVersion.h"
#include "rendering/SceneBuilder.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/SetUpdatesBar.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUndoStack>

#include <memory>

using namespace bld;
using namespace bld::ui;

namespace {

constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;

// SHED and LAMP (8 x 4 studs), and the set YARD of them.
struct YardLibrary {
    QTemporaryDir dir;
    parts::PartsLibrary lib;
    YardLibrary() {
        for (const char* n : { "SHED", "LAMP" }) {
            QFile f(dir.filePath(QStringLiteral("%1.xml").arg(QLatin1String(n))));
            EXPECT_TRUE(f.open(QIODevice::WriteOnly));
            f.write("<part></part>");
            f.close();
            QImage sprite(64, 32, QImage::Format_ARGB32);
            sprite.fill(Qt::darkGray);
            sprite.save(dir.filePath(QStringLiteral("%1.png").arg(QLatin1String(n))));
        }
        writeSet({ "<SubPart id=\"SHED\"><position><x>0</x><y>0</y></position><angle>0</angle></SubPart>",
                   "<SubPart id=\"LAMP\"><position><x>8</x><y>0</y></position><angle>90</angle></SubPart>" });
        lib.addSearchPath(dir.path());
        lib.scan();
    }
    QString setPath() const { return dir.filePath(QStringLiteral("YARD.set.xml")); }
    void writeSet(const QList<const char*>& subparts) {
        QFile f(setPath());
        EXPECT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("<group><Description><en>Yard</en></Description><SubPartList>");
        for (const char* s : subparts) f.write(s);
        f.write("</SubPartList></group>");
    }
    // The set changed on disk (a sync, an edit): the library reads it again.
    void change(const QList<const char*>& subparts) {
        writeSet(subparts);
        lib.forget(QStringLiteral("YARD"));
        lib.scanFile(setPath());
    }
};

const QList<const char*> kThree{
    "<SubPart id=\"SHED\"><position><x>0</x><y>0</y></position><angle>0</angle></SubPart>",
    "<SubPart id=\"LAMP\"><position><x>8</x><y>0</y></position><angle>90</angle></SubPart>",
    "<SubPart id=\"LAMP\"><position><x>16</x><y>4</y></position><angle>0</angle></SubPart>"
};

class SetUpdates : public ::testing::Test {
protected:
    void SetUp() override {
        auto map = std::make_unique<core::Map>();
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = QStringLiteral("layer");
        layer->name = QStringLiteral("Parts");
        map->layers().push_back(std::move(layer));
        view_ = std::make_unique<MapView>(lib_.lib);
        view_->resize(1000, 800);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(map));
        view_->setTrackedSets([](const parts::PartMetadata& m) { return m.kind == parts::PartKind::Group; });
    }
    core::Map& map() { return *view_->currentMap(); }
    core::LayerBrick& layer() { return static_cast<core::LayerBrick&>(*map().layers().front()); }
    bool barShown() const { return view_->setUpdatesBar() && view_->setUpdatesBar()->isVisible(); }
    // The actions of the map's right-click menu on part `guid`.
    QStringList menuOn(const QString& guid) {
        QPoint at;
        for (QGraphicsItem* it : view_->scene()->items())
            if (detail::isBrickItem(it) && it->data(detail::kBrickDataGuid).toString() == guid)
                at = view_->mapFromScene(it->sceneBoundingRect().center());
        QStringList actions;
        QTimer closer;
        closer.setInterval(20);
        QObject::connect(&closer, &QTimer::timeout, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                for (QAction* a : menu->actions()) actions << a->text();
                menu->close();
                closer.stop();
            }
        });
        closer.start();
        QContextMenuEvent ce(QContextMenuEvent::Mouse, at, view_->viewport()->mapToGlobal(at));
        QApplication::sendEvent(view_->viewport(), &ce);
        return actions;
    }

    YardLibrary lib_;
    std::unique_ptr<MapView> view_;
};

} // namespace

TEST_F(SetUpdates, APlacedSetSaysUpdateAvailableAndUpdatesInPlaceInOneStep) {
    view_->addPartAtScenePos(QStringLiteral("YARD"), QPointF(40 * kPx, 20 * kPx));
    ASSERT_EQ(layer().bricks.size(), 2u);
    ASSERT_EQ(map().sidecar.sets.size(), 1u);
    const core::PlacedSet link = map().sidecar.sets.front();
    const QString group = layer().groups.front().guid;
    EXPECT_EQ(link.groupId, group);
    EXPECT_EQ(link.anchorBrick, layer().bricks.front().guid);
    EXPECT_FALSE(barShown());

    // Someone moves it 10 studs right; then the set gets a third part.
    for (auto& b : layer().bricks) b.displayArea.translate(10, 0);
    const QPointF centre = layer().bricks.front().displayArea.center();
    view_->rebuildScene();
    lib_.change(kThree);
    view_->refreshSetUpdates();
    ASSERT_TRUE(barShown());
    EXPECT_EQ(view_->setUpdatesBar()->text(),
              QStringLiteral("<b>Update available:</b> the set “Yard” changed since it was placed."));
    EXPECT_EQ(view_->setUpdatesBar()->updateButton()->text(), QStringLiteral("Update"));
    const auto* help = view_->setUpdatesBar()->findChild<QWidget*>(QStringLiteral("HelpIcon"));
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->property("helpKey").toString(), QStringLiteral("set.updates"));
    // Its right-click menu offers it too (a part of it picks the whole set).
    EXPECT_TRUE(
        menuOn(layer().bricks.front().guid).contains(QStringLiteral("Update set “Yard” (update available)")));

    // Update: the set as it is now, where the copy sits, same group.
    const int steps = view_->undoStack()->count();
    QTest::mouseClick(view_->setUpdatesBar()->updateButton(), Qt::LeftButton);
    ASSERT_EQ(layer().bricks.size(), 3u);
    const auto want = edit::expandSet(lib_.lib, QStringLiteral("YARD"), centre);
    for (size_t i = 0; i < want.bricks.size(); ++i) {
        EXPECT_EQ(layer().bricks[i].partNumber, want.bricks[i].partNumber);
        EXPECT_NEAR(layer().bricks[i].displayArea.center().x(), want.bricks[i].displayArea.center().x(),
                    1e-6);
        EXPECT_NEAR(layer().bricks[i].displayArea.center().y(), want.bricks[i].displayArea.center().y(),
                    1e-6);
        EXPECT_EQ(layer().bricks[i].myGroupId, group);
    }
    ASSERT_EQ(layer().groups.size(), 1u);
    EXPECT_EQ(layer().groups.front().guid, group);
    ASSERT_EQ(map().sidecar.sets.size(), 1u);
    EXPECT_NE(map().sidecar.sets.front().version, link.version);
    EXPECT_EQ(map().sidecar.sets.front().anchorBrick, layer().bricks.front().guid);
    EXPECT_FALSE(barShown());
    EXPECT_EQ(view_->undoStack()->count(), steps + 1);

    // One undo puts the old parts and the offer back; redo brings the new ones.
    view_->undoStack()->undo();
    view_->rebuildScene();
    EXPECT_EQ(layer().bricks.size(), 2u);
    EXPECT_EQ(map().sidecar.sets.front().version, link.version);
    EXPECT_TRUE(barShown());
    view_->undoStack()->redo();
    view_->rebuildScene();
    EXPECT_EQ(layer().bricks.size(), 3u);
    EXPECT_FALSE(barShown());
}

TEST_F(SetUpdates, ADuplicateKeepsTrackAndUpdateAllDoesBoth) {
    view_->addPartAtScenePos(QStringLiteral("YARD"), QPointF(40 * kPx, 20 * kPx));
    for (QGraphicsItem* it : view_->scene()->items())
        if (detail::isBrickItem(it)) it->setSelected(true);
    view_->duplicateSelection();
    ASSERT_EQ(layer().bricks.size(), 4u);
    ASSERT_EQ(map().sidecar.sets.size(), 2u);
    EXPECT_NE(map().sidecar.sets[0].groupId, map().sidecar.sets[1].groupId);

    lib_.change(kThree);
    view_->refreshSetUpdates();
    ASSERT_TRUE(barShown());
    EXPECT_EQ(
        view_->setUpdatesBar()->text(),
        QStringLiteral("<b>Update available:</b> the set “Yard” (2 placed) changed since it was placed."));
    EXPECT_EQ(view_->setUpdatesBar()->updateButton()->text(), QStringLiteral("Update all 2"));
    QTest::mouseClick(view_->setUpdatesBar()->updateButton(), Qt::LeftButton);
    EXPECT_EQ(layer().bricks.size(), 6u);
    EXPECT_FALSE(barShown());
    // "Not now" hides it until the set changes again.
    lib_.change({ "<SubPart id=\"SHED\"><position><x>0</x><y>0</y></position><angle>0</angle></SubPart>" });
    view_->refreshSetUpdates();
    ASSERT_TRUE(barShown());
    QTest::mouseClick(view_->setUpdatesBar()->closeButton(), Qt::LeftButton);
    view_->refreshSetUpdates();
    EXPECT_FALSE(barShown());
}

// A set turned and in a module: the update keeps its turn and the module.
TEST_F(SetUpdates, KeepsTheTurnAndTheModule) {
    auto set = edit::expandSet(lib_.lib, QStringLiteral("YARD"), QPointF(40, 20), 90.0);
    const QString group = set.groups.front().guid;
    auto link = edit::placedSetLink(lib_.lib, QStringLiteral("YARD"), group, set.bricks.front().guid, {});
    ASSERT_TRUE(link);
    QSet<QString> members;
    for (const auto& b : set.bricks) members.insert(b.guid);
    view_->undoStack()->push(
        new edit::AddBricksCommand(map(), 0, std::move(set.bricks), std::move(set.groups)));
    view_->undoStack()->push(new edit::AddPlacedSetsCommand(map(), { *link }));
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Station");
    m.memberIds = members;
    map().sidecar.modules.push_back(m);
    lib_.change(kThree);
    view_->refreshSetUpdates();
    ASSERT_EQ(view_->setUpdates().size(), 1u);
    const auto pose = edit::placedSetPose(layer(), view_->setUpdates().front(), lib_.lib);
    EXPECT_NEAR(pose.centre.x(), 40, 1e-6);
    EXPECT_NEAR(pose.centre.y(), 20, 1e-6);
    EXPECT_NEAR(pose.angle, 90, 1e-6);
    view_->updatePlacedSets(view_->setUpdates());
    ASSERT_EQ(layer().bricks.size(), 3u);
    const auto want = edit::expandSet(lib_.lib, QStringLiteral("YARD"), QPointF(40, 20), 90.0);
    for (size_t i = 0; i < want.bricks.size(); ++i) {
        EXPECT_NEAR(layer().bricks[i].orientation, want.bricks[i].orientation, 1e-4);
        EXPECT_NEAR(layer().bricks[i].displayArea.center().x(), want.bricks[i].displayArea.center().x(),
                    1e-6);
    }
    QSet<QString> now;
    for (const auto& b : layer().bricks) now.insert(b.guid);
    EXPECT_EQ(map().sidecar.modules.front().memberIds, now);
}

// A set inside a set: DEPOT holds YARD and a lamp. A change to YARD offers an
// update for the placed DEPOT; Update brings in the whole new structure, in
// one undo step, and then nothing more is offered.
TEST_F(SetUpdates, AChangeToASetInsideOffersAnUpdateForTheSetAroundIt) {
    {
        QFile f(lib_.dir.filePath(QStringLiteral("DEPOT.set.xml")));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<group><Description><en>Depot</en></Description><SubPartList>"
                "<SubPart id=\"YARD\"><position><x>0</x><y>0</y></position><angle>0</angle></SubPart>"
                "<SubPart id=\"LAMP\"><position><x>0</x><y>12</y></position><angle>0</angle></SubPart>"
                "</SubPartList></group>");
    }
    lib_.lib.scanFile(lib_.dir.filePath(QStringLiteral("DEPOT.set.xml")));
    const auto depot = lib_.lib.metadata(QStringLiteral("DEPOT"));
    ASSERT_TRUE(depot);
    const auto yard = lib_.lib.metadata(QStringLiteral("YARD"));
    ASSERT_TRUE(yard);
    // Its version follows Yard's.
    EXPECT_EQ(edit::trackedSetVersion(lib_.lib, *depot, {}),
              parts::nestedSetVersion(parts::setVersion(*depot), { { QStringLiteral("YARD"), parts::setVersion(*yard) } }));
    // A set that isn't tracked (a library one) doesn't count.
    EXPECT_EQ(edit::trackedSetVersion(lib_.lib, *depot,
                                      [](const parts::PartMetadata& m) { return !m.xmlFilePath.contains(QLatin1String("YARD")); }),
              parts::setVersion(*depot));

    view_->addPartAtScenePos(QStringLiteral("DEPOT"), QPointF(40 * kPx, 20 * kPx));
    ASSERT_EQ(layer().bricks.size(), 3u);
    ASSERT_EQ(layer().groups.size(), 2u);
    ASSERT_EQ(map().sidecar.sets.size(), 1u);
    const core::PlacedSet link = map().sidecar.sets.front();
    view_->refreshSetUpdates();
    EXPECT_FALSE(barShown());

    // Yard, inside it, gets a third part: the depot says so.
    lib_.change(kThree);
    view_->refreshSetUpdates();
    ASSERT_TRUE(barShown());
    EXPECT_EQ(view_->setUpdatesBar()->text(),
              QStringLiteral("<b>Update available:</b> the set “Depot” changed since it was placed."));

    // Update: the whole new structure, one undo step, and no more offer.
    const int steps = view_->undoStack()->count();
    QTest::mouseClick(view_->setUpdatesBar()->updateButton(), Qt::LeftButton);
    EXPECT_EQ(layer().bricks.size(), 4u);
    EXPECT_EQ(layer().groups.size(), 2u);
    EXPECT_EQ(view_->undoStack()->count(), steps + 1);
    EXPECT_NE(map().sidecar.sets.front().version, link.version);
    view_->refreshSetUpdates();
    EXPECT_FALSE(barShown());
    view_->undoStack()->undo();
    view_->rebuildScene();
    EXPECT_EQ(layer().bricks.size(), 3u);
    EXPECT_EQ(map().sidecar.sets.front().version, link.version);
    EXPECT_TRUE(barShown());
}
