// A set you made (Save Selection as Set, in one of your parts folders) can be
// renamed and deleted from its menu in the Parts panel, and the panel shows
// the change at once. Sets in the bundled library or from a server can't.

#include "ui/ConfirmDialog.h"
#include "ui/PartsBrowser.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

using namespace bld;

namespace {

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// A plate, and a set of two of them named `name`, in `dir`.
void writeSet(const QString& dir, const QString& stem, const QByteArray& name) {
    writeFile(dir + QStringLiteral("/PLATE.xml"), "<part><Description><en>Plate</en></Description></part>");
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::gray);
    ASSERT_TRUE(img.save(dir + QStringLiteral("/PLATE.png"), "PNG"));
    writeFile(dir + QLatin1Char('/') + stem + QStringLiteral(".set.xml"),
              "<?xml version=\"1.0\"?>\n<group>\n\t<Description>\n\t\t<en>" + name + "</en>\n\t</Description>\n"
              "\t<SubPartList>\n"
              "\t\t<SubPart id=\"PLATE\"><position><x>0</x><y>0</y></position><angle>0</angle></SubPart>\n"
              "\t\t<SubPart id=\"PLATE\"><position><x>2</x><y>0</y></position><angle>0</angle></SubPart>\n"
              "\t</SubPartList>\n</group>\n");
}

class OwnSets : public ::testing::Test {
protected:
    void SetUp() override {
        mine_ = root_.path() + QStringLiteral("/My parts");
        bundled_ = root_.path() + QStringLiteral("/bundled");
        writeSet(mine_, QStringLiteral("Yard"), "Yard");
        writeSet(bundled_, QStringLiteral("Depot"), "Depot");
        scan();
        browser_ = std::make_unique<ui::PartsBrowser>(lib_);
        browser_->setOwnPartsRoots([this] { return QStringList{ mine_ }; });
        // As MainWindow does: read the parts again, then the panel.
        QObject::connect(browser_.get(), &ui::PartsBrowser::ownSetsChanged, [this] {
            scan();
            browser_->rebuild();
        });
        browser_->rebuild();
    }
    void scan() {
        lib_.clear();
        lib_.addSearchPath(bundled_);
        lib_.addSearchPath(mine_);
        lib_.scan();
    }
    QListWidgetItem* item(const QString& key) {
        for (int i = 0; i < browser_->grid()->count(); ++i)
            if (browser_->grid()->item(i)->data(Qt::UserRole + 1).toString().compare(key, Qt::CaseInsensitive) == 0)
                return browser_->grid()->item(i);
        return nullptr;
    }
    // Right-click `key` in the panel: the menu's item names, and `pick` triggered when given.
    QStringList menu(const QString& key, const QString& pick = {}) {
        browser_->showPart(key);
        QListWidgetItem* it = item(key);
        EXPECT_TRUE(it) << key.toStdString();
        if (!it) return {};
        QStringList names;
        QTimer t;
        QObject::connect(&t, &QTimer::timeout, [&] {
            auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!m) return;
            t.stop();
            QAction* chosen = nullptr;
            for (QAction* a : m->actions()) {
                if (!a->objectName().isEmpty()) names << a->objectName();
                if (!pick.isEmpty() && a->objectName() == pick) chosen = a;
            }
            m->close();
            if (chosen) chosen->trigger();
        });
        t.start(20);
        emit browser_->grid()->customContextMenuRequested(browser_->grid()->visualItemRect(it).center());
        return names;
    }
    // Answers the next confirmation: Delete when `yes`, else Cancel. Returns what it said.
    void answerConfirm(bool yes, QString* body) {
        QTimer::singleShot(50, [yes, body] {
            auto* d = qobject_cast<ui::ConfirmDialog*>(QApplication::activeModalWidget());
            if (!d) return;
            *body = d->bodyText();
            (yes ? d->confirmButton() : d->cancelButton())->click();
        });
    }

    QTemporaryDir root_;
    QString mine_, bundled_;
    parts::PartsLibrary lib_;
    std::unique_ptr<ui::PartsBrowser> browser_;
};

QString setName(parts::PartsLibrary& lib, const QString& key) {
    const auto meta = lib.metadata(key);
    if (!meta) return {};
    for (const auto& d : meta->descriptions)
        if (d.language == QLatin1String("en")) return d.text;
    return {};
}

}  // namespace

TEST_F(OwnSets, OnlyYourOwnSetsCanBeRenamedOrDeleted) {
    EXPECT_TRUE(browser_->isOwnSet(QStringLiteral("Yard")));
    EXPECT_FALSE(browser_->isOwnSet(QStringLiteral("Depot")));  // not in your folders
    EXPECT_FALSE(browser_->isOwnSet(QStringLiteral("PLATE")));  // a part, not a set
    const QStringList yours = menu(QStringLiteral("Yard"));
    EXPECT_TRUE(yours.contains(QStringLiteral("act.set.rename")));
    EXPECT_TRUE(yours.contains(QStringLiteral("act.set.delete")));
    const QStringList bundled = menu(QStringLiteral("Depot"));
    EXPECT_FALSE(bundled.contains(QStringLiteral("act.set.rename")));
    EXPECT_FALSE(bundled.contains(QStringLiteral("act.set.delete")));
}

TEST_F(OwnSets, RenameChangesItsNameAndKeepsItsPartNumber) {
    QString asked;
    browser_->askSetName = [&](const QString& current) {
        asked = current;
        return QStringLiteral("Big yard");
    };
    menu(QStringLiteral("Yard"), QStringLiteral("act.set.rename"));
    EXPECT_EQ(asked, QStringLiteral("Yard"));
    // Same file and number, so layouts that use it are unchanged; the panel shows the new name at once.
    EXPECT_TRUE(QFile::exists(mine_ + QStringLiteral("/Yard.set.xml")));
    EXPECT_EQ(setName(lib_, QStringLiteral("Yard")), QStringLiteral("Big yard"));
    ASSERT_TRUE(item(QStringLiteral("Yard")));
    EXPECT_EQ(item(QStringLiteral("Yard"))->text(), QStringLiteral("Big yard"));

    // Cancelled: nothing changes.
    browser_->askSetName = [](const QString&) { return QString(); };
    menu(QStringLiteral("Yard"), QStringLiteral("act.set.rename"));
    EXPECT_EQ(setName(lib_, QStringLiteral("Yard")), QStringLiteral("Big yard"));
}

TEST_F(OwnSets, DeleteAsksWithTheSetWordingThenRemovesIt) {
    QString body;
    answerConfirm(false, &body);
    menu(QStringLiteral("Yard"), QStringLiteral("act.set.delete"));
    EXPECT_TRUE(body.contains(QStringLiteral("Layouts that use it keep the parts it put down."))) << body.toStdString();
    EXPECT_TRUE(QFile::exists(mine_ + QStringLiteral("/Yard.set.xml")));
    EXPECT_TRUE(item(QStringLiteral("Yard")));

    answerConfirm(true, &body);
    menu(QStringLiteral("Yard"), QStringLiteral("act.set.delete"));
    EXPECT_FALSE(QFile::exists(mine_ + QStringLiteral("/Yard.set.xml")));
    // Gone from the panel at once; its parts stay.
    EXPECT_FALSE(lib_.metadata(QStringLiteral("Yard")));
    EXPECT_FALSE(item(QStringLiteral("Yard")));
    EXPECT_TRUE(QFile::exists(mine_ + QStringLiteral("/PLATE.xml")));
}
