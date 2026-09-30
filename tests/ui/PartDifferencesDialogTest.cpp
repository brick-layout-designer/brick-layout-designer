// The dialog that shows a layout's part beside yours when the two share a
// number but differ.

#include "ui/PartDifferencesDialog.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QPushButton>

using namespace bld;
using Choice = ui::PartDifferencesDialog::Choice;

namespace {

QByteArray xml(const char* author, const char* description) {
    return QByteArray("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n\t<Author>") + author
         + "</Author>\n\t<Description>\n\t\t<fr>Pas celle-ci</fr>\n\t\t<en>" + description
         + "</en>\n\t</Description>\n</part>\n";
}

QByteArray png(int w, int h) {
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QBuffer b;
    b.open(QIODevice::WriteOnly);
    image.save(&b, "PNG");
    return b.data();
}

QList<ui::PartDifferencesDialog::Row> twoRows() {
    return {
        { QStringLiteral("MINE.1"),
          { { QStringLiteral("MINE.1.xml"), xml("Me", "My curve") }, { QStringLiteral("MINE.1.png"), png(320, 160) } },
          { { QStringLiteral("MINE.1.xml"), xml("Someone else", "Their curve") },
            { QStringLiteral("MINE.1.gif"), QByteArray("not an image") } } },
        { QStringLiteral("KIT.1"),
          { { QStringLiteral("KIT.1.set.xml"), xml("Me", "My kit") } },
          { { QStringLiteral("KIT.1.set.xml"), xml("Club", "Club kit") }, { QStringLiteral("KIT.1.png"), png(16, 16) } } },
    };
}

QLabel* labelIn(const QWidget& dlg, const QString& side, const char* name) {
    auto* box = dlg.findChild<QWidget*>(side);
    return box ? box->findChild<QLabel*>(QString::fromLatin1(name)) : nullptr;
}

QPushButton* button(const QDialog& dlg, const QString& text) {
    for (auto* b : dlg.findChildren<QPushButton*>())
        if (b->text() == text) return b;
    return nullptr;
}

}  // namespace

TEST(PartDifferencesDialog, ShowsBothSidesOfEachPart) {
    ui::PartDifferencesDialog dlg(twoRows());
    const auto combos = dlg.findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 2);

    // Previews scaled to fit, or a placeholder where there's no image.
    const QLabel* mine = labelIn(dlg, QStringLiteral("mine-MINE.1"), "preview");
    ASSERT_NE(mine, nullptr);
    ASSERT_FALSE(mine->pixmap().isNull());
    EXPECT_LE(mine->pixmap().width(), 96);
    EXPECT_EQ(mine->pixmap().width(), 2 * mine->pixmap().height());
    const QLabel* broken = labelIn(dlg, QStringLiteral("layouts-MINE.1"), "preview");
    ASSERT_NE(broken, nullptr);
    EXPECT_TRUE(broken->pixmap().isNull());
    EXPECT_EQ(broken->text(), QStringLiteral("No image"));
    EXPECT_TRUE(labelIn(dlg, QStringLiteral("mine-KIT.1"), "preview")->pixmap().isNull());
    EXPECT_FALSE(labelIn(dlg, QStringLiteral("layouts-KIT.1"), "preview")->pixmap().isNull());

    // The English description and the author, from each XML.
    EXPECT_EQ(labelIn(dlg, QStringLiteral("mine-MINE.1"), "info")->text(), QStringLiteral("My curve\nby Me"));
    EXPECT_EQ(labelIn(dlg, QStringLiteral("layouts-MINE.1"), "info")->text(),
              QStringLiteral("Their curve\nby Someone else"));
    EXPECT_EQ(labelIn(dlg, QStringLiteral("layouts-KIT.1"), "info")->text(), QStringLiteral("Club kit\nby Club"));
}

TEST(PartDifferencesDialog, KeepsMineUnlessToldOtherwise) {
    ui::PartDifferencesDialog dlg(twoRows());
    EXPECT_EQ(dlg.choices(), (QMap<QString, Choice>{ { QStringLiteral("MINE.1"), Choice::KeepMine },
                                                     { QStringLiteral("KIT.1"), Choice::KeepMine } }));
    auto* mine = dlg.findChild<QComboBox*>(QStringLiteral("choice-MINE.1"));
    auto* kit = dlg.findChild<QComboBox*>(QStringLiteral("choice-KIT.1"));
    ASSERT_TRUE(mine && kit);
    EXPECT_EQ(mine->currentText(), QStringLiteral("Keep mine"));
    mine->setCurrentText(QStringLiteral("Use the layout's"));
    kit->setCurrentText(QStringLiteral("Keep both"));
    EXPECT_EQ(dlg.choices(), (QMap<QString, Choice>{ { QStringLiteral("MINE.1"), Choice::UseLayouts },
                                                     { QStringLiteral("KIT.1"), Choice::KeepBoth } }));

    QPushButton* apply = button(dlg, QStringLiteral("Apply"));
    ASSERT_NE(apply, nullptr);
    apply->click();
    EXPECT_EQ(dlg.result(), QDialog::Accepted);

    ui::PartDifferencesDialog other(twoRows());
    QPushButton* keepAll = button(other, QStringLiteral("Keep All Mine"));
    ASSERT_NE(keepAll, nullptr);
    keepAll->click();
    EXPECT_EQ(other.result(), QDialog::Rejected);
}

TEST(PartDifferencesDialog, ReadsDescriptionAndAuthor) {
    const auto info = ui::PartDifferencesDialog::partInfo(xml("Me", "My curve"));
    EXPECT_EQ(info.description, QStringLiteral("My curve"));
    EXPECT_EQ(info.author, QStringLiteral("Me"));
    const auto french = ui::PartDifferencesDialog::partInfo(
        "<part><Description><fr>Courbe</fr></Description></part>");
    EXPECT_EQ(french.description, QStringLiteral("Courbe"));
    EXPECT_TRUE(french.author.isEmpty());
    EXPECT_TRUE(ui::PartDifferencesDialog::partInfo("not xml").description.isEmpty());
}
