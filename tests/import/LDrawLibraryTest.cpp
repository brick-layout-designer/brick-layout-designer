#include "import/ldraw/LDrawLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace bld;

namespace {

// Create a minimal LDraw root with the tree layout the resolver
// expects, plus a couple of stub .dat files so resolve() can return
// real paths.
struct LDrawTree {
    QTemporaryDir dir;
    QString root;

    LDrawTree() {
        if (!dir.isValid()) ADD_FAILURE() << "tmp dir failed";
        root = dir.path();
        QDir d(root);
        d.mkpath(QStringLiteral("parts"));
        d.mkpath(QStringLiteral("parts/s"));
        d.mkpath(QStringLiteral("p"));
        d.mkpath(QStringLiteral("p/48"));
        d.mkpath(QStringLiteral("p/8"));
        write(QStringLiteral("LDConfig.ldr"), QByteArray("0 // stub palette\n"));
        write(QStringLiteral("parts/3001.dat"),       QByteArray("0 Brick 2x4\n"));
        write(QStringLiteral("parts/s/3001s01.dat"),  QByteArray("0 Sub of 3001\n"));
        write(QStringLiteral("p/box.dat"),            QByteArray("0 Box primitive\n"));
        write(QStringLiteral("p/48/4-4cyli.dat"),     QByteArray("0 Hi-res cyl\n"));
    }

    void write(const QString& rel, const QByteArray& body) {
        QFile f(QDir(root).absoluteFilePath(rel));
        if (!f.open(QIODevice::WriteOnly)) {
            ADD_FAILURE() << "couldn't write " << rel.toStdString();
            return;
        }
        f.write(body);
    }
};

}

TEST(LDrawLibrary, LooksValidRequiresLDConfigAndParts) {
    import::LDrawLibrary empty;
    EXPECT_FALSE(empty.looksValid());

    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    EXPECT_TRUE(lib.looksValid());
}

TEST(LDrawLibrary, ResolvesPartsBeforePrimitives) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    const QString hit = lib.resolve(QStringLiteral("3001.dat"));
    ASSERT_FALSE(hit.isEmpty());
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/parts/3001.dat")));
}

TEST(LDrawLibrary, ResolvesPrimitiveFromP) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    const QString hit = lib.resolve(QStringLiteral("box.dat"));
    ASSERT_FALSE(hit.isEmpty());
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/p/box.dat")));
}

TEST(LDrawLibrary, ResolvesSubPartViaSubdirHint) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    // Author tools write subparts as "s\3001s01.dat" or "s/3001s01.dat";
    // resolver should split the prefix and find parts/s/.
    const QString hit = lib.resolve(QStringLiteral("s\\3001s01.dat"));
    ASSERT_FALSE(hit.isEmpty());
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/parts/s/3001s01.dat")));
}

TEST(LDrawLibrary, Resolves48HighResPrimitive) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    const QString hit = lib.resolve(QStringLiteral("4-4cyli.dat"));
    ASSERT_FALSE(hit.isEmpty());
    // Bare-name lookup walks parts/ → p/ → p/48 → p/8, so this lands at p/48.
    EXPECT_TRUE(hit.endsWith(QStringLiteral("/p/48/4-4cyli.dat")));
}

TEST(LDrawLibrary, ReturnsEmptyForUnknown) {
    LDrawTree tree;
    import::LDrawLibrary lib(tree.root);
    EXPECT_TRUE(lib.resolve(QStringLiteral("does-not-exist.dat")).isEmpty());
}

TEST(LDrawLibrary, ResolvesUnofficialPartsAfterOfficialOnes) {
    // Studio's library keeps ~24 000 parts under "UnOfficial/".
    LDrawTree t;
    QDir(t.root).mkpath(QStringLiteral("UnOfficial/parts/s"));
    QDir(t.root).mkpath(QStringLiteral("UnOfficial/p"));
    t.write(QStringLiteral("UnOfficial/parts/973tex.dat"), "0 textured torso\n");
    t.write(QStringLiteral("UnOfficial/parts/s/973texs01.dat"), "0 sub\n");
    t.write(QStringLiteral("UnOfficial/parts/3001.dat"), "0 shadowed\n");
    import::LDrawLibrary lib(t.root);
    EXPECT_TRUE(lib.resolve(QStringLiteral("973TEX.DAT")).endsWith(QStringLiteral("UnOfficial/parts/973tex.dat")));
    EXPECT_TRUE(lib.resolve(QStringLiteral("s\\973texs01.dat")).endsWith(QStringLiteral("UnOfficial/parts/s/973texs01.dat")));
    EXPECT_TRUE(lib.resolve(QStringLiteral("3001.dat")).endsWith(QStringLiteral("/parts/3001.dat")));
    EXPECT_FALSE(lib.resolve(QStringLiteral("3001.dat")).contains(QStringLiteral("UnOfficial")));
}

TEST(LDrawLibrary, OverlayDirsWinOverTheLibrary) {
    LDrawTree t;
    QTemporaryDir overlay;
    QFile f(overlay.filePath(QStringLiteral("3001.dat")));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("0 the model's own\n");
    f.close();
    import::LDrawLibrary lib(t.root);
    EXPECT_FALSE(lib.resolve(QStringLiteral("3001.dat")).startsWith(overlay.path()));
    lib.setOverlayDirs({ overlay.path() });
    EXPECT_EQ(lib.resolve(QStringLiteral("3001.DAT")), overlay.filePath(QStringLiteral("3001.dat")));
    EXPECT_TRUE(lib.resolve(QStringLiteral("box.dat")).endsWith(QStringLiteral("p/box.dat")));
    // Overlays alone, no library.
    import::LDrawLibrary bare;
    bare.setOverlayDirs({ overlay.path() });
    EXPECT_EQ(bare.resolve(QStringLiteral("3001.dat")), overlay.filePath(QStringLiteral("3001.dat")));
    EXPECT_TRUE(bare.resolve(QStringLiteral("box.dat")).isEmpty());
}
