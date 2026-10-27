#include "saveload/PartListing.h"
#include "saveload/SetIO.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>

using namespace bld;

namespace {

// Minimal re-parser so the test doesn't need a full PartsLibrary scan.
// Mirrors the shape readSubPartListTag + readPositionBlock in
// PartsLibrary.cpp but over an in-memory <group>.
struct ParsedSubpart {
    QString id;
    QPointF position;
    double  angle = 0.0;
};
struct ParsedSet {
    QString author;
    QString descriptionEn;
    bool canUngroup = false;
    QList<ParsedSubpart> subparts;
};

QPointF readPosition(QXmlStreamReader& r) {
    QPointF p;
    while (r.readNextStartElement()) {
        if      (r.name() == QStringLiteral("x")) p.setX(r.readElementText().toDouble());
        else if (r.name() == QStringLiteral("y")) p.setY(r.readElementText().toDouble());
        else r.skipCurrentElement();
    }
    return p;
}

ParsedSet parseSet(const QString& path) {
    ParsedSet s;
    QFile f(path);
    [&]{ ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text)); }();
    QXmlStreamReader r(&f);
    if (!r.readNextStartElement()) return s;
    if (r.name() != QStringLiteral("group")) return s;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (n == QStringLiteral("Author")) s.author = r.readElementText().trimmed();
        else if (n == QStringLiteral("Description")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("en"))
                    s.descriptionEn = r.readElementText().trimmed();
                else r.skipCurrentElement();
            }
        }
        else if (n == QStringLiteral("CanUngroup"))
            s.canUngroup = (r.readElementText().trimmed().compare(
                QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        else if (n == QStringLiteral("SubPartList")) {
            while (r.readNextStartElement()) {
                if (r.name() != QStringLiteral("SubPart")) { r.skipCurrentElement(); continue; }
                ParsedSubpart sp;
                sp.id = r.attributes().value(QStringLiteral("id")).toString();
                while (r.readNextStartElement()) {
                    if      (r.name() == QStringLiteral("position")) sp.position = readPosition(r);
                    else if (r.name() == QStringLiteral("angle"))    sp.angle = r.readElementText().toDouble();
                    else r.skipCurrentElement();
                }
                s.subparts.push_back(sp);
            }
        }
        else r.skipCurrentElement();
    }
    return s;
}

}  // namespace

TEST(SetIO, WriteMinimalRoundTrips) {
    saveload::SetManifest m;
    m.name = QStringLiteral("Test Set");
    m.author = QStringLiteral("tester");
    m.canUngroup = true;
    saveload::SetSubpart a;
    a.partKey = QStringLiteral("BT R104.8");
    a.positionStuds = QPointF(0, 0);
    a.angleDegrees = 0.0;
    saveload::SetSubpart b;
    b.partKey = QStringLiteral("BT 2865.8");
    b.positionStuds = QPointF(20.19525, -3.647644);
    b.angleDegrees = 708.75;
    m.subparts = { a, b };

    QTemporaryDir dir;
    const QString path = dir.filePath("test.set.xml");
    QString err;
    ASSERT_TRUE(saveload::writeSetXml(path, m, &err)) << err.toStdString();

    const ParsedSet back = parseSet(path);
    EXPECT_EQ(back.author, QStringLiteral("tester"));
    EXPECT_EQ(back.descriptionEn, QStringLiteral("Test Set"));
    EXPECT_TRUE(back.canUngroup);
    ASSERT_EQ(back.subparts.size(), 2);
    EXPECT_EQ(back.subparts[0].id, QStringLiteral("BT R104.8"));
    EXPECT_NEAR(back.subparts[0].position.x(), 0.0, 1e-6);
    EXPECT_NEAR(back.subparts[0].position.y(), 0.0, 1e-6);
    EXPECT_NEAR(back.subparts[0].angle, 0.0, 1e-6);
    EXPECT_EQ(back.subparts[1].id, QStringLiteral("BT 2865.8"));
    EXPECT_NEAR(back.subparts[1].position.x(), 20.19525, 1e-4);
    EXPECT_NEAR(back.subparts[1].position.y(), -3.647644, 1e-4);
    EXPECT_NEAR(back.subparts[1].angle, 708.75, 1e-4);
}

TEST(SetIO, OmitsEmptyMetadataFields) {
    saveload::SetManifest m;
    // Everything default-empty except one subpart.
    saveload::SetSubpart a;
    a.partKey = QStringLiteral("2865.8");
    a.angleDegrees = 0.0;
    m.subparts = { a };

    QTemporaryDir dir;
    const QString path = dir.filePath("bare.set.xml");
    ASSERT_TRUE(saveload::writeSetXml(path, m));

    const ParsedSet back = parseSet(path);
    EXPECT_TRUE(back.author.isEmpty());
    EXPECT_TRUE(back.descriptionEn.isEmpty());
    // CanUngroup defaults to true in the manifest, so it should still serialise.
    EXPECT_TRUE(back.canUngroup);
    ASSERT_EQ(back.subparts.size(), 1);
    EXPECT_EQ(back.subparts[0].id, QStringLiteral("2865.8"));
}

TEST(SetIO, PreservesPrecisionOnMultiDecimalPositions) {
    // BrickTracks sets routinely store 5-6 decimal positions. If the
    // writer rounds to 2 digits, tracks misalign by a stud or more when
    // the set is re-loaded. Guard against regressions on float format.
    saveload::SetManifest m;
    m.name = QStringLiteral("Precision Set");
    for (const QPointF pos : {
            QPointF(-131.4375, -3.811996),
            QPointF(-77.30539,  27.87526),
            QPointF(-137.3951,  44.1042 ) }) {
        saveload::SetSubpart sp;
        sp.partKey = QStringLiteral("TB 2865.8");
        sp.positionStuds = pos;
        sp.angleDegrees = 179.9998;
        m.subparts.push_back(sp);
    }

    QTemporaryDir dir;
    const QString path = dir.filePath("precise.set.xml");
    ASSERT_TRUE(saveload::writeSetXml(path, m));

    const ParsedSet back = parseSet(path);
    ASSERT_EQ(back.subparts.size(), 3);
    for (int i = 0; i < back.subparts.size(); ++i) {
        EXPECT_NEAR(back.subparts[i].position.x(),
                    m.subparts[i].positionStuds.x(), 5e-5)
            << "subpart " << i;
        EXPECT_NEAR(back.subparts[i].position.y(),
                    m.subparts[i].positionStuds.y(), 5e-5)
            << "subpart " << i;
    }
}

TEST(SetIO, RenameChangesOnlyTheEnglishName) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("Yard.set.xml"));
    saveload::SetManifest m;
    m.name = QStringLiteral("Yard");
    m.author = QStringLiteral("Me");
    m.subparts.push_back({ QStringLiteral("3001.1"), QPointF(1.5, -2), 90.0 });
    ASSERT_TRUE(saveload::writeSetXml(path, m));
    const auto before = [&] {
        QFile f(path);
        EXPECT_TRUE(f.open(QIODevice::ReadOnly));
        return f.readAll();
    }();

    QString err;
    ASSERT_TRUE(saveload::renameSetXml(path, QStringLiteral("Big yard & sidings <2>"), &err)) << err.toStdString();
    const ParsedSet s = parseSet(path);
    EXPECT_EQ(s.descriptionEn, QStringLiteral("Big yard & sidings <2>"));
    EXPECT_EQ(s.author, QStringLiteral("Me"));
    ASSERT_EQ(s.subparts.size(), 1);
    EXPECT_EQ(s.subparts[0].id, QStringLiteral("3001.1"));

    // Everything but the name is byte for byte what it was.
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray after = f.readAll();
    after.replace("Big yard &amp; sidings &lt;2&gt;", "Yard");
    EXPECT_EQ(after, before);
}

TEST(SetIO, RenameKeepsOtherLanguagesAndAddsADescriptionWhenThereIsNone) {
    QTemporaryDir dir;
    const QString withFr = dir.filePath(QStringLiteral("A.set.xml"));
    const QString bare = dir.filePath(QStringLiteral("B.set.xml"));
    const auto write = [](const QString& path, const QByteArray& data) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(data);
    };
    write(withFr, "<group><Description><fr>Gare</fr><en>Station</en></Description><SubPartList>"
                  "<SubPart id=\"3001.1\"><position><x>0</x><y>0</y></position><angle>0</angle></SubPart>"
                  "</SubPartList></group>");
    write(bare, "<group><SubPartList>"
                "<SubPart id=\"3001.1\"><position><x>0</x><y>0</y></position><angle>0</angle></SubPart>"
                "</SubPartList></group>");
    ASSERT_TRUE(saveload::renameSetXml(withFr, QStringLiteral("Main station")));
    ASSERT_TRUE(saveload::renameSetXml(bare, QStringLiteral("Siding")));
    EXPECT_EQ(parseSet(withFr).descriptionEn, QStringLiteral("Main station"));
    EXPECT_EQ(parseSet(bare).descriptionEn, QStringLiteral("Siding"));
    EXPECT_EQ(parseSet(bare).subparts.size(), 1);
    QFile f(withFr);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_TRUE(f.readAll().contains("<fr>Gare</fr>"));

    // Not a set: refused, the file untouched.
    const QString part = dir.filePath(QStringLiteral("P.xml"));
    write(part, "<part><Description><en>Brick</en></Description></part>");
    QString err;
    EXPECT_FALSE(saveload::renameSetXml(part, QStringLiteral("X"), &err));
    EXPECT_FALSE(err.isEmpty());
}

TEST(SetIO, AFileStemIsWhatAFileNameCanHold) {
    EXPECT_EQ(saveload::setFileStem(QStringLiteral("Main yard")), QStringLiteral("Main yard"));
    EXPECT_EQ(saveload::setFileStem(QStringLiteral("a/b: c?")), QStringLiteral("a_b_ c_"));
    EXPECT_EQ(saveload::setFileStem(QStringLiteral("Yard.set")), QStringLiteral("Yard"));
    EXPECT_EQ(saveload::setFileStem(QStringLiteral(" . ")), QStringLiteral("Set"));
}

TEST(SetIO, AFreeNameCountsOnFromTheTakenOne) {
    const QStringList taken{ QStringLiteral("yard"), QStringLiteral("yard (2)"), QStringLiteral("depot") };
    const auto isTaken = [&](const QString& stem) { return taken.contains(stem.toLower()); };
    EXPECT_EQ(saveload::freeSetName(QStringLiteral("Yard"), isTaken), QStringLiteral("Yard (3)"));
    EXPECT_EQ(saveload::freeSetName(QStringLiteral("Depot"), isTaken), QStringLiteral("Depot (2)"));
    // "Yard (2)" goes on to "Yard (3)", not "Yard (2) (2)".
    EXPECT_EQ(saveload::freeSetName(QStringLiteral("Yard (2)"), isTaken), QStringLiteral("Yard (3)"));
    EXPECT_EQ(saveload::freeSetName(QStringLiteral("a/b"), isTaken), QStringLiteral("a/b (2)"));
}

TEST(SetIO, ASetCantBeNamedLikeOneOfItsOwnParts) {
    const QStringList subparts{ QStringLiteral("3001.1"), QStringLiteral("YARDPLATE"),
                                QStringLiteral("Inner.set") };
    EXPECT_EQ(saveload::partNamedLikeSet(QStringLiteral("yardplate"), subparts), QStringLiteral("YARDPLATE"));
    EXPECT_EQ(saveload::partNamedLikeSet(QStringLiteral(" 3001.1 "), subparts), QStringLiteral("3001.1"));
    // A set inside it, by its file name: "Inner.set" is the set "Inner".
    EXPECT_EQ(saveload::partNamedLikeSet(QStringLiteral("inner"), subparts), QStringLiteral("Inner.set"));
    EXPECT_TRUE(saveload::partNamedLikeSet(QStringLiteral("Yard plates"), subparts).isEmpty());
    EXPECT_TRUE(saveload::partNamedLikeSet(QStringLiteral("3001"), subparts).isEmpty());
}

TEST(SetIO, ASetFileNamesItsParts) {
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("Y.set.xml"));
    saveload::SetManifest m;
    m.name = QStringLiteral("Y");
    m.subparts = { { QStringLiteral("3001.1"), QPointF(0, 0), 0.0 },
                   { QStringLiteral("YARDPLATE"), QPointF(8, 0), 90.0 } };
    ASSERT_TRUE(saveload::writeSetXml(path, m));
    EXPECT_EQ(saveload::setSubpartKeys(path),
              (QStringList{ QStringLiteral("3001.1"), QStringLiteral("YARDPLATE") }));
    EXPECT_TRUE(saveload::setSubpartKeys(QDir(dir.path()).filePath(QStringLiteral("none.xml"))).isEmpty());
}

TEST(SetIO, RenamedXmlChangesOnlyTheEnglishName) {
    const QString xml = QStringLiteral("<group><Description><fr>Gare</fr><en>Station</en></Description>"
                                       "<SubPartList/></group>");
    const auto renamed = saveload::renamedSetXml(xml, QStringLiteral("Big & small"));
    ASSERT_TRUE(renamed);
    EXPECT_EQ(*renamed, QStringLiteral("<group><Description><fr>Gare</fr><en>Big &amp; small</en></Description>"
                                       "<SubPartList/></group>"));
    EXPECT_FALSE(saveload::renamedSetXml(QStringLiteral("<part/>"), QStringLiteral("X")));
}

// <NotListedInLibrary> set or taken out of a part's or set's XML, as the
// web's server does it (withNotListed in apps/server/src/utils/partXml.ts).
TEST(PartListing, SetsOrClearsNotListedInLibrary) {
    const QString xml = QStringLiteral("<?xml version=\"1.0\"?>\n<part>\n\t<Author>Me</Author>\n</part>");
    const QString hidden = saveload::withNotListed(xml, true);
    EXPECT_EQ(hidden, QStringLiteral("<?xml version=\"1.0\"?>\n<part>\n\t<NotListedInLibrary>true</NotListedInLibrary>\n\t<Author>Me</Author>\n</part>"));
    EXPECT_EQ(saveload::withNotListed(hidden, true), hidden);  // once
    EXPECT_EQ(saveload::withNotListed(hidden, false), xml);
    EXPECT_EQ(saveload::withNotListed(QStringLiteral("<group><SubPartList/></group>"), true),
              QStringLiteral("<group>\n\t<NotListedInLibrary>true</NotListedInLibrary><SubPartList/></group>"));
    EXPECT_EQ(saveload::withNotListed(QStringLiteral("<part> <NotListedInLibrary/></part>"), false), QStringLiteral("<part></part>"));

    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("P.xml"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(xml.toUtf8());
    f.close();
    ASSERT_TRUE(saveload::setNotListed(path, true));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString::fromUtf8(f.readAll()), hidden);
    QString error;
    EXPECT_FALSE(saveload::setNotListed(QDir(dir.path()).filePath(QStringLiteral("none.xml")), true, &error));
    EXPECT_FALSE(error.isEmpty());
}
