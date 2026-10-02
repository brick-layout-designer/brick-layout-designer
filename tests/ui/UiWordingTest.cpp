// The words the user sees agree with the web app. Every tr("...") string in
// src/ is scanned:
//   - a map is made of "sheets", never "layers" (the file format and the
//     code still say Layer; the screen does not);
//   - "Venue library" / "Module library" use the web's casing.

#include <gtest/gtest.h>

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

namespace {

struct Hit {
    QString file;
    QString text;
};

// Every literal passed to tr(...) or QObject::tr(...) in the C++ sources.
QList<Hit> trStrings() {
    QList<Hit> out;
    static const QRegularExpression re(QStringLiteral(R"re(\btr\(\s*"((?:[^"\\]|\\.)*)")re"));
    QDirIterator it(QStringLiteral(BLD_SOURCE_DIR "/src"), { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString text = QString::fromUtf8(f.readAll());
        for (auto m = re.globalMatch(text); m.hasNext();) {
            out.push_back({ path.mid(QStringLiteral(BLD_SOURCE_DIR).size() + 1), m.next().captured(1) });
        }
    }
    return out;
}

QString describe(const QList<Hit>& hits) {
    QStringList lines;
    for (const Hit& h : hits) lines << h.file + QStringLiteral(": \"") + h.text + QLatin1Char('"');
    return lines.join(QLatin1Char('\n'));
}

} // namespace

TEST(UiWordingTest, FindsTheStrings) {
    // Guard against the scan silently finding nothing.
    EXPECT_GT(trStrings().size(), 500);
}

TEST(UiWordingTest, SaysSheetNotLayer) {
    static const QRegularExpression layer(QStringLiteral(R"(\blayers?\b)"),
                                          QRegularExpression::CaseInsensitiveOption);
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (layer.match(h.text).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "Say \"sheet\", as the web does:\n" << describe(bad).toStdString();
}

TEST(UiWordingTest, LibraryCasingMatchesTheWeb) {
    static const QRegularExpression wrong(QStringLiteral(R"(\b(Venue|Module) &?Library\b)"));
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (wrong.match(h.text).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "Say \"Venue library\" / \"Module library\":\n" << describe(bad).toStdString();
}

// "Library" alone is ambiguous now that there are parts folders, a Module
// library and a Venue library: every "library" names which one it is.
TEST(UiWordingTest, EveryLibrarySaysWhichOne) {
    static const QRegularExpression named(
        QStringLiteral(R"re(\b(Module|Venue|LDraw|Studio|parts?) &?librar(y|ies)\b|library\.ldraw\.org(/library)?)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression library(QStringLiteral(R"(\blibrar(y|ies)\b)"),
                                            QRegularExpression::CaseInsensitiveOption);
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        QString rest = h.text;
        rest.remove(named);
        if (library.match(rest).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "Say which library (Module library, Venue library, parts folders…):\n"
                               << describe(bad).toStdString();
}

// The jargon BlueBrick used for modules and budgets is gone.
TEST(UiWordingTest, ModuleAndBudgetWordsArePlain) {
    static const QRegularExpression jargon(QStringLiteral(R"(Flatten|Clone [Mm]odule|Select Members|Budget &?Limitation|Library Part)"));
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (jargon.match(h.text).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << describe(bad).toStdString();
}
