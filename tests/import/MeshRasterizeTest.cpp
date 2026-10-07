#include "geom/Mesh.h"
#include "import/mesh/MeshRasterize.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QMargins>

using namespace bld;

TEST(MeshRasterize, EmptyMeshReturnsNullImage) {
    geom::Mesh m;
    const auto r = import::rasterizeMeshTopDown(m);
    EXPECT_TRUE(r.image.isNull());
}

TEST(MeshRasterize, SingleTriangleProducesColouredPixels) {
    geom::Mesh m;
    geom::Triangle t;
    // 1-stud (20 LDU) right triangle in the xz plane.
    t.v[0] = { 0,  0,  0 };
    t.v[1] = { 20, 0, 0 };
    t.v[2] = { 0,  0, 20 };
    t.color = QColor::fromRgb(255, 0, 0);
    m.tris.push_back(t);

    const auto r = import::rasterizeMeshTopDown(m);
    ASSERT_FALSE(r.image.isNull());
    EXPECT_EQ(r.meshBoundsXZ.width(),  1.0);
    EXPECT_EQ(r.meshBoundsXZ.height(), 1.0);

    // At default 8 px/stud + 2 px margin → ~12×12 image. Sample
    // somewhere clearly inside the triangle (a couple of px right /
    // down of the top-left corner) and verify it's red-ish.
    QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
    const QColor px = img.pixelColor(3, 3);
    EXPECT_GT(px.red(), 200);
    EXPECT_LT(px.green(), 50);
    EXPECT_LT(px.blue(), 50);
    EXPECT_GT(px.alpha(), 200);
}

TEST(MeshRasterize, HigherYTrianglePaintsOver) {
    geom::Mesh m;
    geom::Triangle low, hi;
    // Two overlapping squares (split into 2 tris each); low one is
    // green at Y=0, high one is blue at Y=10. Full overlap so every
    // pixel of the visible result should be blue.
    low.color = QColor(0, 200, 0);
    hi.color  = QColor(0, 0, 200);
    low.v[0] = { 0,  0, 0 };
    low.v[1] = { 20, 0, 0 };
    low.v[2] = { 0,  0, 20 };
    hi.v[0] = { 0,  10, 0 };
    hi.v[1] = { 20, 10, 0 };
    hi.v[2] = { 0,  10, 20 };
    m.tris.push_back(low);
    m.tris.push_back(hi);

    const auto r = import::rasterizeMeshTopDown(m);
    ASSERT_FALSE(r.image.isNull());
    QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
    const QColor px = img.pixelColor(3, 3);
    // Sort puts high-Y last → blue should win.
    EXPECT_LT(px.green(), 50);
    EXPECT_GT(px.blue(), 150);
}

namespace {

// A flat rectangle at height `y` (LDU, +Y up), split into two triangles.
void addRect(geom::Mesh& m, double x0, double z0, double x1, double z1, double y, QColor c) {
    geom::Triangle a, b;
    a.v[0] = { x0, y, z0 }; a.v[1] = { x1, y, z0 }; a.v[2] = { x1, y, z1 };
    b.v[0] = { x0, y, z0 }; b.v[1] = { x1, y, z1 }; b.v[2] = { x0, y, z1 };
    a.color = b.color = c;
    m.tris.push_back(a);
    m.tris.push_back(b);
}

}  // namespace

TEST(MeshRasterize, TheBottomLayersStudsStayOnTheGrid) {
    // Aaron: a 16 x 16 baseplate with a roof overhanging it by half a stud
    // on the left. The overhang adds a whole stud; the baseplate keeps its
    // studs on the grid.
    geom::Mesh m;
    addRect(m, 0, 0, 320, 320, 0, Qt::green);       // baseplate, studs 0..16
    addRect(m, -10, 40, 200, 200, 80, Qt::red);     // roof, half a stud past the left edge
    import::RasterizeOptions opt;
    opt.pxPerStud = 8;
    opt.marginPx = 0;
    opt.ssaa = 1;
    const auto r = import::rasterizeMeshTopDown(m, opt);
    ASSERT_FALSE(r.image.isNull());
    EXPECT_EQ(r.spriteStuds, QRectF(-1, 0, 17, 16));
    EXPECT_EQ(r.snapMargin, QMargins(1, 0, 0, 0));
    EXPECT_EQ(r.image.size(), QSize(17 * 8, 16 * 8));
    // The baseplate's left edge is pixel column 8 exactly: stud 1 of the sprite.
    const QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
    EXPECT_EQ(img.pixelColor(7, 100).alpha(), 0);
    EXPECT_GT(img.pixelColor(8, 100).alpha(), 200);
    EXPECT_GT(img.pixelColor(4, 40).alpha(), 200) << "the roof's overhang is drawn";
}

TEST(MeshRasterize, WithoutAFlatWholeStudBottomTheBoundsAreCentred) {
    // A base 3.5 studs wide isn't on a stud lattice: the old rule, the
    // bounds padded to whole studs and centred.
    geom::Mesh m;
    addRect(m, 0, 0, 70, 40, 0, Qt::green);
    const auto r = import::rasterizeMeshTopDown(m);
    EXPECT_EQ(r.spriteStuds, QRectF(-0.25, 0, 4, 2));
    EXPECT_EQ(r.snapMargin, QMargins());
    EXPECT_FALSE(import::baseLattice(m));
}

TEST(MeshRasterize, LddsShortBricksStillFindTheirStuds) {
    // LDD shapes stop 0.1 mm short of the stud on each side; the base of a
    // 2 x 4 brick offset by half a stud still lands on a whole-stud lattice.
    geom::Mesh m;
    addRect(m, 10.25, 0.25, 89.75, 39.75, 0, Qt::red);
    addRect(m, 10.25, 0.25, 89.75, 39.75, 24, Qt::red);
    const auto base = import::baseLattice(m);
    ASSERT_TRUE(base);
    EXPECT_DOUBLE_EQ(base->left(), 0.5);
    EXPECT_DOUBLE_EQ(base->top(), 0.0);
    EXPECT_DOUBLE_EQ(base->width(), 4.0);
    EXPECT_DOUBLE_EQ(base->height(), 2.0);
    const auto r = import::rasterizeMeshTopDown(m);
    EXPECT_EQ(r.spriteStuds, QRectF(0.5, 0, 4, 2));
}

TEST(MeshRasterize, ASmallPartHangingBelowIsntTheBottomLayer) {
    // LEGO 70620's lowest piece is a small part turned 45° hanging below
    // the baseplates; the baseplates are still the layer that goes on the grid.
    geom::Mesh m;
    addRect(m, 0, 0, 640, 640, 0, Qt::green);  // 32 x 32 baseplate
    geom::Triangle t;                          // a small diamond 100 LDU below
    t.v[0] = { 100, -100, 80 };
    t.v[1] = { 120, -100, 100 };
    t.v[2] = { 100, -100, 120 };
    t.color = Qt::black;
    m.tris.push_back(t);
    const auto base = import::baseLattice(m);
    ASSERT_TRUE(base);
    EXPECT_EQ(*base, QRectF(0, 0, 32, 32));
}

TEST(MeshRasterize, SpecksJustBelowTheBaseDontWidenIt) {
    // A small flat piece 2 LDU above the baseplate's underside (inside the
    // band the bottom layer takes in), sticking out past its edge: the
    // baseplate alone sets the lattice.
    geom::Mesh m;
    addRect(m, 0, 0, 640, 640, 0, Qt::green);
    addRect(m, -15, 100, 5, 120, 2, Qt::black);
    const auto base = import::baseLattice(m);
    ASSERT_TRUE(base);
    EXPECT_EQ(*base, QRectF(0, 0, 32, 32));
}

TEST(MeshRasterize, SeeThroughPartsShowWhatIsBelow) {
    // Aaron: a Trans-Clear plate over a red brick shows the red through it,
    // tinted, instead of hiding it; where nothing is below, the sprite stays
    // see-through too.
    geom::Mesh m;
    addRect(m, 0, 0, 40, 40, 0, QColor(200, 0, 0));                  // red brick, 2 x 2
    addRect(m, 0, 0, 80, 40, 24, QColor(255, 255, 255, 128));         // Trans-Clear plate, 4 x 2
    addRect(m, 40, 0, 80, 40, 30, QColor(0, 0, 255, 64));            // Trans-Blue over its right half
    import::RasterizeOptions opt;
    opt.pxPerStud = 8;
    opt.marginPx = 0;
    opt.ssaa = 1;
    const auto r = import::rasterizeMeshTopDown(m, opt);
    const QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
    const QColor overRed = img.pixelColor(8, 8);
    EXPECT_EQ(overRed.alpha(), 255);
    EXPECT_GT(overRed.red(), 200);  // red and white blended: pink
    EXPECT_GT(overRed.green(), 100);
    EXPECT_LT(overRed.green(), 160);
    // Over nothing: half see-through white, then blue on top of that.
    const QColor overNothing = img.pixelColor(24, 8);
    EXPECT_GT(overNothing.alpha(), 128);
    EXPECT_LT(overNothing.alpha(), 255);
    EXPECT_GT(overNothing.blue(), overNothing.red());
    // The see-through plate doesn't hide the red brick's edges either: the
    // depth buffer keeps the brick.
    EXPECT_EQ(r.spriteStuds.width(), 4);
}

TEST(MeshRasterize, OpaqueStillHidesWhatIsBelow) {
    geom::Mesh m;
    // The top one first: the depth buffer, not the order, decides.
    addRect(m, 0, 0, 40, 40, 24, QColor(0, 200, 0));
    addRect(m, 0, 0, 40, 40, 0, QColor(200, 0, 0));
    import::RasterizeOptions opt;
    opt.pxPerStud = 8;
    opt.marginPx = 0;
    opt.ssaa = 1;
    const QImage img = import::rasterizeMeshTopDown(m, opt).image.convertToFormat(QImage::Format_ARGB32);
    EXPECT_EQ(img.pixelColor(8, 8), QColor(0, 200, 0));
}
