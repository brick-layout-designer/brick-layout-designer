// Writes starting inputs for the fuzzers that have no fixture files:
//   bld_fuzz_seeds <dir>   ->  <dir>/{sidecar,venue,budget,lxfml,studio}/...
// fuzz/run.sh adds the fixtures (maps, parts, budgets) and runs them.

#include "core/Sidecar.h"
#include "edit/Budget.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <private/qzipwriter_p.h>

using namespace bld;

namespace {

core::Venue sampleVenue() {
    core::Venue v;
    v.name = QStringLiteral("Hall");
    core::VenueEdge wall;
    wall.polyline = { { 0, 0 }, { 400, 0 }, { 400, 300 } };
    core::VenueEdge door;
    door.kind = core::EdgeKind::Door;
    door.doorWidthStuds = 40;
    door.polyline = { { 400, 300 }, { 0, 300 }, { 0, 0 } };
    v.edges = { wall, door };
    core::VenueObstacle pillar;
    pillar.polygon = { { 100, 100 }, { 120, 100 }, { 120, 120 } };
    pillar.label = QStringLiteral("pillar");
    v.obstacles = { pillar };
    v.layoutBoundsStuds = QRectF(10, 10, 200, 100);
    return v;
}

const char* kLxfml = R"(<?xml version="1.0" encoding="UTF-8"?>
<LXFML name="seed">
  <Bricks>
    <Brick designID="3001"><Part materials="4"><Bone transformation="1,0,0,0,1,0,0,0,1,40,0,0"/></Part></Brick>
    <Brick designID="3020"><Part materials="21,0"><Bone transformation="0,0,1,0,1,0,-1,0,0,0,9.6,8"/></Part></Brick>
  </Bricks>
</LXFML>
)";

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const QDir root(QString::fromLocal8Bit(argv[1]));
    for (const char* sub : { "sidecar", "venue", "budget", "lxfml", "studio" }) root.mkpath(QLatin1String(sub));

    core::Sidecar sidecar;
    core::AnchoredLabel label;
    label.id = QStringLiteral("L1");
    label.text = QStringLiteral("Station");
    label.kind = core::AnchorKind::Brick;
    label.targetId = QStringLiteral("123");
    label.offset = { 1, 2 };
    sidecar.anchoredLabels.push_back(label);
    core::Module mod;
    mod.id = QStringLiteral("M1");
    mod.name = QStringLiteral("Yard");
    mod.memberIds = { QStringLiteral("1"), QStringLiteral("2") };
    sidecar.modules.push_back(mod);
    sidecar.venue = sampleVenue();
    saveload::writeSidecar(root.filePath(QStringLiteral("sidecar/seed.bbm.bld")), QByteArray("bbm"), sidecar);

    saveload::writeVenueFile(root.filePath(QStringLiteral("venue/seed.bldvenue")), sampleVenue());

    edit::Budget budget;
    budget.setLimit(QStringLiteral("2865.8"), 12);
    budget.setLimit(QStringLiteral("3811.1"), 0);
    budget.write(root.filePath(QStringLiteral("budget/seed.bbb")));

    QFile lx(root.filePath(QStringLiteral("lxfml/seed.lxfml")));
    if (lx.open(QIODevice::WriteOnly)) lx.write(kLxfml);

    // Studio .io: a zip with the model as model.ldr.
    QZipWriter zip(root.filePath(QStringLiteral("studio/seed.io")));
    zip.addFile(QStringLiteral("model.ldr"), QByteArray("0 seed\n1 4 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"));
    zip.close();
    return 0;
}
