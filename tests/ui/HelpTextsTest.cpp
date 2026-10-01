// The help-text catalogue: the same keys and the same words as the web's
// (apps/web/src/help/helpTexts.ts, whose key list is fixtures/
// help-keys.txt), short texts fit a tooltip, and every key the UI uses
// is in the catalogue.

#include "ui/help/HelpTexts.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

using namespace bld::ui::help;

namespace {

QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

// fixtures/help-keys.txt: one key per line; # starts a comment.
QStringList fixtureKeys() {
    QStringList keys;
    const QString text = readFile(QStringLiteral(BLD_SOURCE_DIR "/fixtures/help-keys.txt"));
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString key = line.trimmed();
        if (!key.isEmpty() && !key.startsWith(QLatin1Char('#'))) keys << key;
    }
    return keys;
}

// The web repo's catalogue, when it is checked out next to this one (or
// named by BLD_WEB_HELP_TEXTS).
QString webCatalogue() {
    const QString env = qEnvironmentVariable("BLD_WEB_HELP_TEXTS");
    if (!env.isEmpty()) return readFile(env);
    return readFile(QStringLiteral(BLD_SOURCE_DIR
                                   "/../collaborative-layout-designer-web/apps/web/src/help/helpTexts.ts"));
}

}  // namespace

TEST(HelpTexts, HasTheWebsKeysInTheSameOrder) {
    const QStringList fixture = fixtureKeys();
    ASSERT_EQ(fixture.size(), 51);
    EXPECT_EQ(sharedHelpKeys(), fixture);
}

TEST(HelpTexts, DesktopOnlyKeysAreSeparateAndUnique) {
    const QStringList all = helpKeys();
    EXPECT_EQ(QSet<QString>(all.begin(), all.end()).size(), all.size());
    for (const QString& key : desktopOnlyHelpKeys()) EXPECT_FALSE(sharedHelpKeys().contains(key)) << key.toStdString();
    EXPECT_FALSE(desktopOnlyHelpKeys().isEmpty());
}

TEST(HelpTexts, EveryEntryIsShortAndComplete) {
    for (const QString& key : helpKeys()) {
        const auto e = helpEntry(key);
        ASSERT_TRUE(e) << key.toStdString();
        EXPECT_FALSE(e->title.isEmpty()) << key.toStdString();
        EXPECT_FALSE(e->shortText.isEmpty()) << key.toStdString();
        EXPECT_FALSE(e->more.isEmpty()) << key.toStdString();
        EXPECT_LE(e->shortText.size(), 140) << key.toStdString();
        // Learn more points at an in-app help page, never a server.
        if (!e->learnMoreUrl.isEmpty()) EXPECT_TRUE(e->learnMoreUrl.startsWith(QLatin1String("/help#")));
    }
    EXPECT_FALSE(helpEntry(QStringLiteral("no.such.key")));
}

// Spot checks that run everywhere (the full comparison needs the web repo).
TEST(HelpTexts, SpotChecksMatchTheWeb) {
    const auto sheets = helpEntry(QStringLiteral("panel.sheets"));
    ASSERT_TRUE(sheets);
    EXPECT_EQ(sheets->title, QStringLiteral("Sheets"));
    EXPECT_EQ(sheets->shortText, QStringLiteral("Sheets are see-through pages stacked on the map, to keep things apart."));
    EXPECT_EQ(sheets->learnMoreUrl, QStringLiteral("/help#sheets"));
    const auto bbm = helpEntry(QStringLiteral("download.bbm"));
    ASSERT_TRUE(bbm);
    EXPECT_EQ(bbm->more, QStringLiteral("BlueBrick can’t hold everything this app can, so the venue, labels, modules "
                                        "and background picture are left out. Your layout here keeps them."));
    const auto choice = helpEntry(QStringLiteral("partsDiffer.choice"));
    ASSERT_TRUE(choice);
    EXPECT_EQ(choice->shortText, QStringLiteral("Keep the server’s part, use the file’s, or keep both."));
    EXPECT_TRUE(choice->learnMoreUrl.isEmpty());
}

// Word for word against the web's helpTexts.ts, when it's there.
TEST(HelpTexts, MatchesTheWebCatalogueWordForWord) {
    const QString ts = webCatalogue();
    if (ts.isEmpty()) GTEST_SKIP() << "the web repo's helpTexts.ts isn't checked out next to this one";
    static const QRegularExpression entryRe(QStringLiteral(R"(^  '([^']+)': \{\n(.*?)^  \},)"),
                                            QRegularExpression::MultilineOption
                                                | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression fieldRe(QStringLiteral(R"(^    (\w+): '([^']*)',)"),
                                            QRegularExpression::MultilineOption);
    QStringList webKeys;
    for (auto it = entryRe.globalMatch(ts); it.hasNext();) {
        const auto m = it.next();
        const QString key = m.captured(1);
        webKeys << key;
        QHash<QString, QString> fields;
        for (auto f = fieldRe.globalMatch(m.captured(2)); f.hasNext();) {
            const auto fm = f.next();
            fields.insert(fm.captured(1), fm.captured(2));
        }
        // The web's words; the desktop's own overrides are checked below.
        const auto e = sharedHelpEntry(key);
        ASSERT_TRUE(e) << key.toStdString();
        EXPECT_EQ(e->title, fields.value(QStringLiteral("title"))) << key.toStdString();
        EXPECT_EQ(e->shortText, fields.value(QStringLiteral("short"))) << key.toStdString();
        EXPECT_EQ(e->more, fields.value(QStringLiteral("more"))) << key.toStdString();
        EXPECT_EQ(e->learnMoreUrl, fields.value(QStringLiteral("learnMoreUrl"))) << key.toStdString();
    }
    EXPECT_EQ(webKeys, sharedHelpKeys());
}

// A shared key shows the web's words unless it is in the one override
// list (kDesktopOverrides in HelpTexts.cpp), and every override there
// really changes something about a shared key.
TEST(HelpTexts, SharedKeysUseTheWebsWordsExceptTheListedOverrides) {
    const QStringList overridden = desktopOverriddenHelpKeys();
    EXPECT_EQ(QSet<QString>(overridden.begin(), overridden.end()).size(), overridden.size());
    for (const QString& key : sharedHelpKeys()) {
        const auto shown = helpEntry(key);
        const auto web = sharedHelpEntry(key);
        ASSERT_TRUE(shown && web) << key.toStdString();
        const bool same = shown->title == web->title && shown->shortText == web->shortText
                          && shown->more == web->more && shown->learnMoreUrl == web->learnMoreUrl;
        EXPECT_EQ(same, !overridden.contains(key)) << key.toStdString();
    }
    for (const QString& key : overridden) EXPECT_TRUE(sharedHelpKeys().contains(key)) << key.toStdString();
    for (const QString& key : desktopOnlyHelpKeys()) EXPECT_FALSE(sharedHelpEntry(key)) << key.toStdString();
}

// The desktop exports every view into a folder, not a zip file.
TEST(HelpTexts, ExportAllViewsSaysFolderOnTheDesktop) {
    const auto shown = helpEntry(QStringLiteral("share.exportAllViews"));
    const auto web = sharedHelpEntry(QStringLiteral("share.exportAllViews"));
    ASSERT_TRUE(shown && web);
    EXPECT_EQ(shown->shortText, QStringLiteral("One picture of every saved view, into a folder you choose."));
    EXPECT_EQ(web->shortText, QStringLiteral("One picture of every saved view, all in one zip file."));
    EXPECT_EQ(shown->title, web->title);
    EXPECT_EQ(shown->more, web->more);
}

// Every key a "?" is given in the code (HelpButton, withHelp,
// headingWithHelp, addToMessageBox, HelpButton::addTo and
// PanelHeader::install take the key as their first string literal). Any
// dotted literal counts, digits and extra dots too, so a typo like
// "room.units2" or "status.sheet.old" can't slip past.
TEST(HelpTexts, EveryKeyTheUiUsesExists) {
    static const QRegularExpression callRe(
        QStringLiteral(R"re(\b(?:HelpButton|withHelp|headingWithHelp|addToMessageBox|addTo|PanelHeader::install)\()re"
                       R"re([^;]*?QStringLiteral\("([A-Za-z][\w-]*(?:\.[\w-]+)+)"\))re"));
    const QStringList known = helpKeys();
    QSet<QString> used;
    QDirIterator it(QStringLiteral(BLD_SOURCE_DIR "/src"), { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QLatin1String("HelpTexts.cpp"))) continue;
        const QString text = readFile(path);
        for (auto m = callRe.globalMatch(text); m.hasNext();) {
            const QString key = m.next().captured(1);
            used.insert(key);
            EXPECT_TRUE(known.contains(key)) << key.toStdString() << " in " << path.toStdString();
        }
    }
    // The scan found the "?"s (so a broken pattern can't pass quietly).
    EXPECT_GE(used.size(), 40);
}
