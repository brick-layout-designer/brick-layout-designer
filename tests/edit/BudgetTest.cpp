// Budget (.bbb) files and rules, against vanilla BlueBrick's own files
// (fixtures/bluebrick-oracle/budget*.bbb, saved by BlueBrick 1.9.2).

#include "edit/Budget.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace bld;

namespace {

QString oracle(const QString& name) {
    return QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + name;
}

QByteArray bytes(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

TEST(BudgetTest, SavesExactlyLikeBlueBrick) {
    QTemporaryDir tmp;
    for (const char* name : { "budget.bbb", "budget-empty.bbb" }) {
        SCOPED_TRACE(name);
        const auto budget = edit::Budget::read(oracle(QLatin1String(name)));
        ASSERT_TRUE(budget);
        const QString out = tmp.filePath(QLatin1String(name));
        ASSERT_TRUE(budget->write(out));
        EXPECT_EQ(bytes(out), bytes(oracle(QLatin1String(name))));
    }
}

TEST(BudgetTest, ReadsLimitsInFileOrder) {
    const auto budget = edit::Budget::read(oracle(QStringLiteral("budget.bbb")));
    ASSERT_TRUE(budget);
    ASSERT_EQ(budget->entries().size(), 3);
    EXPECT_EQ(budget->entries()[0].first, QStringLiteral("2865.8"));
    EXPECT_EQ(budget->limit(QStringLiteral("2865.8")), 12);
    EXPECT_EQ(budget->limit(QStringLiteral("3811.1")), 0);
    EXPECT_EQ(budget->limit(QStringLiteral("ts_oldname_test")), 3) << "part ids ignore case";
    EXPECT_EQ(budget->limit(QStringLiteral("3001.1")), -1) << "unlisted: unlimited by default";
    EXPECT_EQ(budget->limit(QStringLiteral("3001.1"), false), 0) << "unlisted: forbidden";
}

TEST(BudgetTest, RejectsOtherFiles) {
    EXPECT_FALSE(edit::Budget::read(oracle(QStringLiteral("flex-in.bbm"))));
    EXPECT_FALSE(edit::Budget::read(oracle(QStringLiteral("does-not-exist.bbb"))));
}

TEST(BudgetTest, OldPartNamesReadAsTheCurrentOnes) {
    const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
    if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    parts::PartsLibrary lib;
    lib.addSearchPath(root);
    lib.scan();
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("old.bbb"));
    edit::Budget old;
    old.setLimit(QStringLiteral("7857.7"), 4);  // renamed to 992.7
    ASSERT_TRUE(old.write(path));
    const auto budget = edit::Budget::read(path, &lib);
    ASSERT_TRUE(budget);
    EXPECT_EQ(budget->limit(QStringLiteral("992.7")), 4);
}

TEST(BudgetTest, MergeAddsLimitsLikeBlueBrick) {
    edit::Budget a, b;
    a.setLimit(QStringLiteral("A"), 1);
    a.setLimit(QStringLiteral("B"), 2);
    b.setLimit(QStringLiteral("b"), 5);
    b.setLimit(QStringLiteral("C"), 3);
    a.mergeWith(b);
    ASSERT_EQ(a.entries().size(), 3);
    EXPECT_EQ(a.entries()[0], qMakePair(QStringLiteral("A"), 1));
    EXPECT_EQ(a.entries()[1], qMakePair(QStringLiteral("B"), 7)) << "summed and moved last, then C";
    EXPECT_EQ(a.entries()[2], qMakePair(QStringLiteral("C"), 3));
}

TEST(BudgetTest, LimitationCountsSetsByTheirParts) {
    edit::Budget budget;
    budget.setLimit(QStringLiteral("2865.8"), 3);
    budget.setLimit(QStringLiteral("MYSET"), 5);
    QHash<QString, int> usage{ { QStringLiteral("2865.8"), 2 } };
    const auto noSets = [](const QString&) { return QHash<QString, int>{}; };
    EXPECT_TRUE(edit::canAddToBudget(budget, usage, QStringLiteral("2865.8"), 1, true, noSets));
    EXPECT_FALSE(edit::canAddToBudget(budget, usage, QStringLiteral("2865.8"), 2, true, noSets));
    EXPECT_TRUE(edit::canAddToBudget(budget, usage, QStringLiteral("3001.1"), 1, true, noSets));
    EXPECT_FALSE(edit::canAddToBudget(budget, usage, QStringLiteral("3001.1"), 1, false, noSets));
    // A set with two straights doesn't fit: only one is left.
    const auto set = [](const QString& p) {
        return p == QLatin1String("MYSET") ? QHash<QString, int>{ { QStringLiteral("2865.8"), 2 } }
                                           : QHash<QString, int>{};
    };
    EXPECT_FALSE(edit::canAddToBudget(budget, usage, QStringLiteral("MYSET"), 1, true, set));
}

TEST(BudgetTest, ViolationsListPartsOverTheirLimit) {
    core::Map map;
    auto layer = std::make_unique<core::LayerBrick>();
    for (int i = 0; i < 3; ++i) {
        core::Brick b;
        b.partNumber = QStringLiteral("2865.8");
        layer->bricks.push_back(b);
    }
    map.layers().push_back(std::move(layer));
    edit::Budget budget;
    budget.setLimit(QStringLiteral("2865.8"), 2);
    budget.setLimit(QStringLiteral("3001.1"), 0);
    const auto v = edit::checkBudget(map, budget);
    ASSERT_EQ(v.size(), 1);
    EXPECT_EQ(v[0].used, 3);
    EXPECT_EQ(v[0].overBy, 1);
}
