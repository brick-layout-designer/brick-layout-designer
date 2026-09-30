// The compare window for reconnecting with offline edits.

#include "CompareDialog.h"
#include "ui/help/HelpButton.h"
#include "ui/help/HelpTexts.h"

#include "core/Map.h"
#include "sync/SyncDoc.h"
#include "sync/WebModel.h"

#include <gtest/gtest.h>

#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QTreeWidget>

using namespace bld;
using namespace bld::sync::merge;
using bld::sync::CompareDialog;

namespace {

const QString kDir = QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/compare/");

Snapshot load(const QString& name) {
    QFile f(kDir + name + QStringLiteral(".bin"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    sync::SyncDoc doc;
    QString error;
    EXPECT_TRUE(doc.applyUpdate(f.readAll(), &error)) << error.toStdString();
    auto map = sync::mapFromDocJson(doc.toJson(), &error);
    EXPECT_TRUE(map) << error.toStdString();
    return map ? snapshotOf(*map) : Snapshot{};
}

QList<ItemChange> fixtureChanges() {
    return compareLayouts(load(QStringLiteral("base")), load(QStringLiteral("mine")), load(QStringLiteral("server")));
}

constexpr int kKeyRole = Qt::UserRole + 1;

QString keyOf(const QTreeWidgetItem* row) { return row->data(0, kKeyRole).toString(); }

QComboBox* comboOf(const CompareDialog& dlg, int row) {
    return qobject_cast<QComboBox*>(dlg.list()->itemWidget(dlg.list()->topLevelItem(row), 1));
}

QComboBox* comboFor(const CompareDialog& dlg, const QString& key) {
    for (int i = 0; i < dlg.list()->topLevelItemCount(); ++i)
        if (keyOf(dlg.list()->topLevelItem(i)) == key) return comboOf(dlg, i);
    return nullptr;
}

}  // namespace

// Clashes first, then mine, then the server's (and both-the-same) greyed,
// with a choice only on mine and the clashes.
TEST(CompareDialog, ListsClashesThenMineThenTheServers) {
    const auto changes = fixtureChanges();
    CompareDialog dlg(changes);
    auto* list = dlg.list();
    ASSERT_EQ(list->topLevelItemCount(), changes.size());

    QList<Status> order;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        const QString key = keyOf(list->topLevelItem(i));
        for (const auto& c : changes)
            if (c.key == key) order << c.status;
    }
    ASSERT_EQ(order.size(), changes.size());
    const QList<Status> expected = { Status::Conflict, Status::Conflict, Status::Conflict, Status::Mine,
                                     Status::Mine,     Status::Mine,     Status::Mine,     Status::Mine,
                                     Status::Same,     Status::Server,   Status::Server,   Status::Server };
    EXPECT_EQ(order, expected);

    const QColor grey = dlg.palette().color(QPalette::Disabled, QPalette::Text);
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        const bool choosable = order[i] == Status::Conflict || order[i] == Status::Mine;
        EXPECT_EQ(comboOf(dlg, i) != nullptr, choosable) << keyOf(list->topLevelItem(i)).toStdString();
        if (!choosable) {
            EXPECT_EQ(list->topLevelItem(i)->foreground(0).color(), grey);
        }
    }
    EXPECT_EQ(dlg.choices().size(), 8);
}

TEST(CompareDialog, ClashesDefaultToTheServerAndMineToApply) {
    CompareDialog dlg(fixtureChanges());
    const auto choices = dlg.choices();
    for (const QString key : { QStringLiteral("brick:211:224"), QStringLiteral("label:L-both"), QStringLiteral("map") })
        EXPECT_EQ(choices.value(key, Choice::Mine), Choice::Server) << key.toStdString();
    for (const QString key : { QStringLiteral("brick:211:222"), QStringLiteral("brick:211:223"),
                               QStringLiteral("brick:211:mine-new-brick"), QStringLiteral("label:L-mine"),
                               QStringLiteral("text:3144:T0") })
        EXPECT_EQ(choices.value(key, Choice::Server), Choice::Mine) << key.toStdString();
}

TEST(CompareDialog, OffersKeepBothOnlyWhereAllowed) {
    const auto changes = fixtureChanges();
    CompareDialog dlg(changes);
    int both = 0;
    for (const auto& c : changes) {
        auto* combo = comboFor(dlg, c.key);
        if (!combo) continue;
        const bool offered = combo->findData(static_cast<int>(Choice::Both)) >= 0;
        EXPECT_EQ(offered, c.status == Status::Conflict && c.allowsBoth()) << c.key.toStdString();
        both += offered;
    }
    // brick:211:224 and label:L-both, not the map header.
    EXPECT_EQ(both, 2);
    EXPECT_LT(comboFor(dlg, QStringLiteral("map"))->findData(static_cast<int>(Choice::Both)), 0);
}

TEST(CompareDialog, ChooseAndChooseAllSetTheChoices) {
    CompareDialog dlg(fixtureChanges());
    dlg.chooseAll(Choice::Mine);
    for (auto c : dlg.choices()) EXPECT_EQ(c, Choice::Mine);
    dlg.chooseAll(Choice::Server);
    for (auto c : dlg.choices()) EXPECT_EQ(c, Choice::Server);

    dlg.choose(QStringLiteral("brick:211:224"), Choice::Both);
    dlg.choose(QStringLiteral("text:3144:T0"), Choice::Mine);
    // Not offered there: left as it was.
    dlg.choose(QStringLiteral("map"), Choice::Both);
    const auto choices = dlg.choices();
    EXPECT_EQ(choices.value(QStringLiteral("brick:211:224")), Choice::Both);
    EXPECT_EQ(choices.value(QStringLiteral("text:3144:T0")), Choice::Mine);
    EXPECT_EQ(choices.value(QStringLiteral("map")), Choice::Server);
    EXPECT_EQ(choices.value(QStringLiteral("label:L-both")), Choice::Server);
}

TEST(CompareDialog, FinishSetsTheActionAndLaterRejects) {
    const auto changes = fixtureChanges();
    for (const auto action : { CompareDialog::Action::Apply, CompareDialog::Action::Discard,
                               CompareDialog::Action::ReplaceServer, CompareDialog::Action::SaveAsNew }) {
        CompareDialog dlg(changes);
        dlg.finish(action);
        EXPECT_EQ(dlg.action(), action);
        EXPECT_EQ(dlg.result(), QDialog::Accepted);
    }
    CompareDialog dlg(changes);
    dlg.finish(CompareDialog::Action::Apply);
    dlg.finish(CompareDialog::Action::Later);
    EXPECT_EQ(dlg.action(), CompareDialog::Action::Later);
    EXPECT_EQ(dlg.result(), QDialog::Rejected);
}

TEST(CompareDialog, PickingARowHighlightsItsChange) {
    CompareDialog dlg(fixtureChanges());
    QStringList seen;
    QObject::connect(&dlg, &CompareDialog::highlight, [&](const ItemChange& c) { seen << c.key; });
    dlg.list()->setCurrentItem(dlg.list()->topLevelItem(0));
    dlg.list()->setCurrentItem(dlg.list()->topLevelItem(dlg.list()->topLevelItemCount() - 1));
    ASSERT_EQ(seen.size(), 2);
    EXPECT_EQ(seen[0], keyOf(dlg.list()->topLevelItem(0)));
    EXPECT_EQ(seen[1], keyOf(dlg.list()->topLevelItem(dlg.list()->topLevelItemCount() - 1)));
}

// The intro counts changes in plain English: "1 change", "3 changes".
TEST(CompareDialog, IntroCountsInPlainEnglish) {
    const auto intro = [](std::initializer_list<Status> statuses) {
        QList<ItemChange> changes;
        int i = 0;
        for (const auto s : statuses) {
            ItemChange c;
            c.key = QStringLiteral("label:%1").arg(i++);
            c.kind = QStringLiteral("label");
            c.status = s;
            changes << c;
        }
        CompareDialog dlg(changes);
        const auto* label = dlg.findChild<QLabel*>(QStringLiteral("intro"));
        return label ? label->text() : QString();
    };
    const QString one = intro({ Status::Mine, Status::Server });
    EXPECT_TRUE(one.startsWith(QStringLiteral("While you were offline you made 1 change. It doesn't clash")))
        << one.toStdString();
    const QString three = intro({ Status::Mine, Status::Mine, Status::Mine });
    EXPECT_TRUE(three.startsWith(QStringLiteral("While you were offline you made 3 changes. None of them")))
        << three.toStdString();
    const QString lone = intro({ Status::Conflict });
    EXPECT_TRUE(lone.startsWith(QStringLiteral("While you were offline you made 1 change, and it clashes")))
        << lone.toStdString();
    const QString oneClash = intro({ Status::Mine, Status::Conflict });
    EXPECT_TRUE(oneClash.startsWith(QStringLiteral("While you were offline you made 2 changes; 1 of them clashes")))
        << oneClash.toStdString();
    const QString twoClash = intro({ Status::Mine, Status::Conflict, Status::Conflict });
    EXPECT_TRUE(twoClash.startsWith(QStringLiteral("While you were offline you made 3 changes; 2 of them clash ")))
        << twoClash.toStdString();
    for (const auto& text : { one, three, lone, oneClash, twoClash })
        EXPECT_FALSE(text.contains(QStringLiteral("(s)"))) << text.toStdString();
}

// The "?"s: what the window is, and what the choices mean.
TEST(CompareDialog, HasHelpForTheWindowAndTheChoices) {
    CompareDialog dlg(fixtureChanges());
    QStringList keys;
    for (auto* b : dlg.findChildren<bld::ui::help::HelpButton*>()) {
        keys << b->key();
        EXPECT_TRUE(bld::ui::help::helpEntry(b->key())) << b->key().toStdString();
    }
    keys.sort();
    EXPECT_EQ(keys, QStringList({ QStringLiteral("compare.about"), QStringLiteral("compare.keep") }));
}
