#include "import/ldraw/LDrawLibrary.h"
#include "import/ldraw/LDrawMeshLoader.h"
#include "import/ldraw/LDrawPalette.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace bld;

namespace {

// Build a minimal LDraw root in a temp dir so the loader has real
// files to resolve against. Each helper writes one .dat file.
struct LDrawTree {
    QTemporaryDir dir;
    QString root;

    LDrawTree() {
        if (!dir.isValid()) ADD_FAILURE() << "tmp dir failed";
        root = dir.path();
        QDir(root).mkpath(QStringLiteral("parts"));
        QDir(root).mkpath(QStringLiteral("p"));
        // Bundled LDrawPalette fallback covers code 1 (blue) etc.;
        // we don't write LDConfig here since we test palette in its
        // own suite.
    }

    void writePart(const QString& name, const QByteArray& body) {
        write(QStringLiteral("parts/") + name, body);
    }
    void writePrim(const QString& name, const QByteArray& body) {
        write(QStringLiteral("p/") + name, body);
    }
    void write(const QString& rel, const QByteArray& body) {
        QFile f(QDir(root).absoluteFilePath(rel));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(body);
    }
};

}

TEST(LDrawMeshLoader, ParsesSingleTriangle) {
    LDrawTree tree;
    tree.writePart(QStringLiteral("simple.dat"), QByteArray(
        "0 Simple\n"
        "3 1 0 0 0 10 0 0 0 0 10\n"
    ));
    import::LDrawLibrary lib(tree.root);
    import::LDrawPalette pal;
    import::LDrawMeshLoader loader(lib, pal);
    const auto mesh = loader.loadPart(QStringLiteral("simple.dat"));
    ASSERT_EQ(mesh.tris.size(), 1u);
    EXPECT_DOUBLE_EQ(mesh.tris[0].v[1].x, 10.0);
    EXPECT_DOUBLE_EQ(mesh.tris[0].v[2].z, 10.0);
    // Code 1 is blue.
    EXPECT_LT(mesh.tris[0].color.red(), 30);
    EXPECT_GT(mesh.tris[0].color.blue(), 100);
}

TEST(LDrawMeshLoader, SplitsQuadIntoTwoTriangles) {
    LDrawTree tree;
    tree.writePart(QStringLiteral("quad.dat"), QByteArray(
        "4 1 0 0 0  10 0 0  10 0 10  0 0 10\n"
    ));
    import::LDrawLibrary lib(tree.root);
    import::LDrawPalette pal;
    import::LDrawMeshLoader loader(lib, pal);
    const auto mesh = loader.loadPart(QStringLiteral("quad.dat"));
    EXPECT_EQ(mesh.tris.size(), 2u);
}

TEST(LDrawMeshLoader, WalksSubfileRefAndAppliesTransform) {
    LDrawTree tree;
    // Primitive sits at origin in its own file; the part references it
    // with a translation of (100, 0, 0). Bake should produce a tri at
    // x=100 / x=110 / x=100 etc.
    tree.writePrim(QStringLiteral("box.dat"), QByteArray(
        "3 16 0 0 0  10 0 0  0 0 10\n"
    ));
    tree.writePart(QStringLiteral("part.dat"), QByteArray(
        "1 4 100 0 0 1 0 0 0 1 0 0 0 1 box.dat\n"
    ));
    import::LDrawLibrary lib(tree.root);
    import::LDrawPalette pal;
    import::LDrawMeshLoader loader(lib, pal);
    const auto mesh = loader.loadPart(QStringLiteral("part.dat"));
    ASSERT_EQ(mesh.tris.size(), 1u);
    EXPECT_DOUBLE_EQ(mesh.tris[0].v[0].x, 100.0);
    EXPECT_DOUBLE_EQ(mesh.tris[0].v[1].x, 110.0);
    // Inherited color 16 → use parent (code 4 = red).
    EXPECT_GT(mesh.tris[0].color.red(), 150);
    EXPECT_LT(mesh.tris[0].color.green(), 80);
}

TEST(LDrawMeshLoader, RecordsErrorForUnresolvedSubfile) {
    LDrawTree tree;
    tree.writePart(QStringLiteral("brokenpart.dat"), QByteArray(
        "1 4 0 0 0 1 0 0 0 1 0 0 0 1 missing.dat\n"
    ));
    import::LDrawLibrary lib(tree.root);
    import::LDrawPalette pal;
    import::LDrawMeshLoader loader(lib, pal);
    const auto mesh = loader.loadPart(QStringLiteral("brokenpart.dat"));
    EXPECT_TRUE(mesh.tris.empty());
    EXPECT_FALSE(loader.errors().isEmpty());
    EXPECT_TRUE(loader.errors().first().contains(QStringLiteral("missing.dat")));
}

TEST(LDrawMeshLoader, CachesParsedDatFiles) {
    LDrawTree tree;
    tree.writePrim(QStringLiteral("box.dat"), QByteArray(
        "3 16 0 0 0  10 0 0  0 0 10\n"
    ));
    // Two refs to the same primitive — the cache should ensure we
    // only parse box.dat once but still bake two triangles.
    tree.writePart(QStringLiteral("twice.dat"), QByteArray(
        "1 1 0 0 0  1 0 0 0 1 0 0 0 1 box.dat\n"
        "1 1 50 0 0 1 0 0 0 1 0 0 0 1 box.dat\n"
    ));
    import::LDrawLibrary lib(tree.root);
    import::LDrawPalette pal;
    import::LDrawMeshLoader loader(lib, pal);
    const auto mesh = loader.loadPart(QStringLiteral("twice.dat"));
    EXPECT_EQ(mesh.tris.size(), 2u);
    EXPECT_DOUBLE_EQ(mesh.tris[0].v[0].x, 0.0);
    EXPECT_DOUBLE_EQ(mesh.tris[1].v[0].x, 50.0);
}
