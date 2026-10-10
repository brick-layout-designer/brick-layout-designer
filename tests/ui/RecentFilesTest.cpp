// File › Open Recent: a file that has gone since is explained and taken off
// the list, instead of failing with "No such file or directory".
#include "parts/PartsLibrary.h"
#include "ui/MainWindow.h"
#include "ui/UpdateCheck.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

using namespace bld;
using namespace bld::ui;

namespace {

QAction* recentAction(MainWindow& w, const QString& text) {
    for (QAction* top : w.menuBar()->actions()) {
        if (!top->menu()) continue;
        for (QAction* a : top->menu()->actions()) {
            if (!a->menu() || !a->text().contains(QLatin1String("Recent"))) continue;
            for (QAction* r : a->menu()->actions())
                if (r->text() == text) return r;
        }
    }
    return nullptr;
}

} // namespace

TEST(RecentFiles, AFileThatIsGoneIsExplainedAndTakenOffTheList) {
    QStandardPaths::setTestModeEnabled(true);
    UpdateCheck::setCheckAtStartupEnabled(false);
    QTemporaryDir dir;
    const QString gone = dir.filePath(QStringLiteral("gone.bld-layout"));
    QSettings().setValue(QStringLiteral("recent/list"), QStringList{ gone });
    {
        parts::PartsLibrary lib;
        MainWindow w(lib);
        QAction* a = recentAction(w, QStringLiteral("gone.bld-layout"));
        ASSERT_NE(a, nullptr);
        QString text;
        QTimer::singleShot(0, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                text = box->text();
                box->accept();
            }
        });
        a->trigger();
        // (The box's title isn't checked: macOS leaves message box titles out.)
        EXPECT_TRUE(text.contains(QLatin1String("isn't there any more"))) << text.toStdString();
        EXPECT_TRUE(QSettings().value(QStringLiteral("recent/list")).toStringList().isEmpty());
        EXPECT_EQ(recentAction(w, QStringLiteral("gone.bld-layout")), nullptr);
    }
    QSettings().remove(QStringLiteral("recent/list"));
    QStandardPaths::setTestModeEnabled(false);
}

TEST(RecentFiles, SaysWhichServerALayoutCameFrom) {
    QStandardPaths::setTestModeEnabled(true);
    UpdateCheck::setCheckAtStartupEnabled(false);
    QTemporaryDir dir;
    const QString fromServer = dir.filePath(QStringLiteral("city.bld-layout"));
    const QString local = dir.filePath(QStringLiteral("home.bld-layout"));
    QSettings().setValue(QStringLiteral("recent/list"), QStringList{ fromServer, local });
    QSettings().setValue(QStringLiteral("recent/servers"), QVariantMap{ { fromServer, QStringLiteral("https://club.example") } });
    {
        parts::PartsLibrary lib;
        MainWindow w(lib);
        EXPECT_NE(recentAction(w, QStringLiteral("city.bld-layout — on club.example")), nullptr);
        EXPECT_NE(recentAction(w, QStringLiteral("home.bld-layout")), nullptr);
    }
    QSettings().remove(QStringLiteral("recent/list"));
    QSettings().remove(QStringLiteral("recent/servers"));
    QStandardPaths::setTestModeEnabled(false);
}
