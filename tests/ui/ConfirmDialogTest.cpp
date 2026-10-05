// The one confirmation dialog (ui/ConfirmDialog.h): what it says, Cancel
// and Esc, focus on Cancel, typing the name, and the local Module
// library's Delete going through it.

#include "ui/ConfirmDialog.h"
#include "ui/ModuleLibraryPanel.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace bld::ui;

TEST(ConfirmDialog, SaysWhatGoesWhatStaysAndWhetherItComesBack) {
    DeleteWording w = ConfirmDialog::moduleWording();
    ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("Bench"), w));
    EXPECT_EQ(d.findChild<QLabel*>(QStringLiteral("confirmTitle"))->text(), QStringLiteral("Delete “Bench”?"));
    EXPECT_TRUE(d.bodyText().contains(QStringLiteral("Layouts that already use it don’t change.")));
    EXPECT_TRUE(d.bodyText().contains(QStringLiteral("This can’t be undone.")));
    EXPECT_EQ(d.confirmButton()->text(), QStringLiteral("Delete"));
    EXPECT_TRUE(d.confirmButton()->property("danger").toBool());
    EXPECT_EQ(d.nameEdit(), nullptr);
    EXPECT_FALSE(d.confirmButton()->isDefault());
    EXPECT_FALSE(d.confirmButton()->autoDefault());
    d.show();
    QApplication::setActiveWindow(&d);
    EXPECT_EQ(QApplication::focusWidget(), d.cancelButton());
}

TEST(ConfirmDialog, CancelAndEscSayNoDeleteSaysYes) {
    {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("x")));
        d.show();
        d.cancelButton()->click();
        EXPECT_EQ(d.result(), QDialog::Rejected);
    }
    {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("x")));
        d.show();
        QTest::keyClick(&d, Qt::Key_Escape);
        EXPECT_EQ(d.result(), QDialog::Rejected);
        EXPECT_FALSE(d.isVisible());
    }
    {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("x")));
        d.show();
        d.confirmButton()->click();
        EXPECT_EQ(d.result(), QDialog::Accepted);
    }
}

TEST(ConfirmDialog, ABigDeletionNeedsTheNameTyped) {
    ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("Main Yard"), ConfirmDialog::layoutWording()));
    d.show();
    ASSERT_NE(d.nameEdit(), nullptr);
    EXPECT_FALSE(d.confirmButton()->isEnabled());
    d.nameEdit()->setText(QStringLiteral("Main"));
    EXPECT_FALSE(d.confirmButton()->isEnabled());
    emit d.nameEdit()->returnPressed();
    EXPECT_TRUE(d.isVisible());
    d.nameEdit()->setText(QStringLiteral("  main yard "));
    EXPECT_TRUE(d.confirmButton()->isEnabled());
    emit d.nameEdit()->returnPressed();
    EXPECT_EQ(d.result(), QDialog::Accepted);
}

TEST(ConfirmDialog, Words) {
    DeleteWording w;
    w.verb = QStringLiteral("Remove");
    const auto o = ConfirmDialog::deleteOptions(QStringLiteral("Ann"), w);
    EXPECT_EQ(o.title, QStringLiteral("Remove “Ann”?"));
    EXPECT_EQ(o.confirmLabel, QStringLiteral("Remove"));
    EXPECT_EQ(o.removes, QStringLiteral("“Ann” is deleted."));
    w.title = QStringLiteral("Cancel the invite?");
    EXPECT_EQ(ConfirmDialog::deleteOptions(QStringLiteral("x"), w).title, QStringLiteral("Cancel the invite?"));
    EXPECT_TRUE(ConfirmDialog::typedMatches(QStringLiteral(" ABC "), QStringLiteral("abc")));
    EXPECT_FALSE(ConfirmDialog::typedMatches(QStringLiteral("ab"), QStringLiteral("abc")));
    // The same words as the website's.
    EXPECT_EQ(ConfirmDialog::venueWording().keeps, QStringLiteral("Layouts made from it keep their own copy of the venue."));
    EXPECT_EQ(ConfirmDialog::customPartWording().keeps, QStringLiteral("Layouts that use it show a placeholder in its place."));
    EXPECT_TRUE(ConfirmDialog::layoutWording().typeName);
}

TEST(ConfirmDialog, AskWaitsForTheAnswer) {
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<ConfirmDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->confirmButton()->click();
    });
    EXPECT_TRUE(ConfirmDialog::confirmDelete(nullptr, QStringLiteral("Bench")));
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<ConfirmDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->cancelButton()->click();
    });
    EXPECT_FALSE(ConfirmDialog::confirmDelete(nullptr, QStringLiteral("Bench")));
}

TEST(ModuleLibraryPanel, DeletesAModuleFileOnlyAfterAYes) {
    QTemporaryDir dir;
    const QString bbm = dir.filePath(QStringLiteral("Bench.bbm"));
    const QString sidecar = bld::saveload::sidecarPathFor(bbm);
    for (const QString& p : { bbm, sidecar }) {
        QFile f(p);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    const QString before = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
    ModuleLibraryPanel panel;
    panel.setLibraryPath(dir.path());
    ASSERT_EQ(panel.list()->count(), 1);
    QStringList asked;
    bool answer = false;
    panel.setConfirm([&](const QString& name) {
        asked << name;
        return answer;
    });
    EXPECT_FALSE(panel.deleteModule(bbm));
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Bench") });
    EXPECT_TRUE(QFile::exists(bbm));
    answer = true;
    EXPECT_TRUE(panel.deleteModule(bbm));
    EXPECT_FALSE(QFile::exists(bbm));
    EXPECT_FALSE(QFile::exists(sidecar));
    EXPECT_EQ(panel.list()->item(0)->flags(), Qt::NoItemFlags);  // "No modules here yet."
    QSettings().setValue(QStringLiteral("modules/libraryPath"), before);
}
