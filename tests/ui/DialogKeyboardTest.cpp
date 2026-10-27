// Keyboard and screen reader use of the dialogs: every control has a name
// a screen reader can say, Tab reaches every control in reading order,
// Escape closes, Enter has a default button to press, and no two
// shortcut letters (&File, &Fit) clash within a dialog.

#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "ui/BudgetDialog.h"
#include "ui/BudgetSession.h"
#include "ui/ConfirmDialog.h"
#include "ui/DownloadCenterDialog.h"
#include "ui/FindDialog.h"
#include "ui/LibraryPathsDialog.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"
#include "ui/ModuleLibraryDialogs.h"
#include "ui/PartDifferencesDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/SaveSetDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/SharePictureDialog.h"
#include "ui/VenueDialog.h"
#include "ui/VenueDimensionsDialog.h"
#include "ui/help/ShortcutsDialog.h"
#include "ui/theme/AppPrefs.h"
#include "ui/tours/Tours.h"
#include "ui/venue/VenueDesignerDialog.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QTabWidget>
#include <QAccessible>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>
#include <functional>
#include <memory>

using namespace bld;

namespace {

bool takesTab(const QWidget* w, const QWidget* dialog) {
    return w->isVisibleTo(dialog) && w->isEnabled() && (w->focusPolicy() & Qt::TabFocus) && w->window() == dialog
        && !w->focusProxy();
}

QString describe(const QWidget* w) {
    QString what = QString::fromLatin1(w->metaObject()->className());
    if (!w->objectName().isEmpty()) what += QLatin1Char('#') + w->objectName();
    if (const auto* b = qobject_cast<const QAbstractButton*>(w); b && !b->text().isEmpty())
        what += QStringLiteral(" \"") + b->text() + QLatin1Char('"');
    if (!w->toolTip().isEmpty()) what += QStringLiteral(" tip=\"") + w->toolTip().left(40) + QLatin1Char('"');
    return what;
}

QString accessibleName(QWidget* w) {
    QAccessibleInterface* a = QAccessible::queryAccessibleInterface(w);
    return a ? a->text(QAccessible::Name).trimmed() : QString();
}

// A name a screen reader can say: not empty, and more than "…" or "?".
bool sayable(const QString& name) {
    return std::any_of(name.cbegin(), name.cend(), [](QChar c) { return c.isLetterOrNumber(); });
}

// One of a set of choices only one of which is on (Tab stops on the one
// that is on; the arrow keys move between them).
bool inExclusiveSet(const QWidget* w) {
    const auto* b = qobject_cast<const QAbstractButton*>(w);
    return b && b->isCheckable() && (b->autoExclusive() || (b->group() && b->group()->exclusive()));
}

// The shortcut letter in "&Save" (none for "&&" or no '&').
QChar mnemonic(const QString& text) {
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != QLatin1Char('&')) continue;
        if (text[i + 1] == QLatin1Char('&')) {
            ++i;
            continue;
        }
        return text[i + 1].toLower();
    }
    return {};
}

struct Problems {
    QStringList unnamed, unreachable, clashes;
    bool escapeCloses = true;
    QString escapeFocus;
    bool hasDefault = true;
    QString all() const {
        QStringList out;
        for (const auto& u : unnamed) out << QStringLiteral("no accessible name: ") + u;
        for (const auto& u : unreachable) out << QStringLiteral("not reached by Tab: ") + u;
        for (const auto& c : clashes) out << QStringLiteral("shortcut letter clash: ") + c;
        if (!escapeCloses) out << QStringLiteral("Escape doesn't close it (focus on %1)").arg(escapeFocus);
        if (!hasDefault) out << QStringLiteral("no default button for Enter");
        return out.join(QLatin1Char('\n'));
    }
};

Problems audit(QDialog& dialog, bool needsDefault = true) {
    Problems p;
    dialog.show();
    EXPECT_TRUE(QTest::qWaitForWindowExposed(&dialog));
    QApplication::processEvents();

    const auto widgets = dialog.findChildren<QWidget*>();
    QList<QWidget*> tabbable;
    for (QWidget* w : widgets)
        if (takesTab(w, &dialog)) tabbable << w;

    // Names, on every page of every tab widget too.
    QSet<QString> named;
    const auto checkNames = [&] {
        for (QWidget* w : dialog.findChildren<QWidget*>())
            if (takesTab(w, &dialog) && !sayable(accessibleName(w))) {
                const QString d = describe(w) + QStringLiteral(" (said as \"") + accessibleName(w) + QStringLiteral("\")");
                if (!named.contains(d)) p.unnamed << d;
                named.insert(d);
            }
    };
    checkNames();
    for (QTabWidget* tabs : dialog.findChildren<QTabWidget*>()) {
        if (!tabs->isVisibleTo(&dialog)) continue;
        const int was = tabs->currentIndex();
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            checkNames();
        }
        tabs->setCurrentIndex(was);
    }
    // Tab moves on out of a table (the arrow keys move between its cells).
    for (auto* view : dialog.findChildren<QAbstractItemView*>())
        if (view->isVisibleTo(&dialog) && view->tabKeyNavigation() && view->model() && view->model()->rowCount() > 0)
            p.unreachable << describe(view) + QStringLiteral(": Tab stays inside it");

    // Tab reaches every control.
    QSet<QWidget*> reached;
    QWidget* first = dialog.focusWidget();
    if (!first && !tabbable.isEmpty()) {
        tabbable.front()->setFocus(Qt::TabFocusReason);
        first = tabbable.front();
    }
    if (first) {
        QWidget* at = first;
        for (int i = 0; i < 4 * widgets.size() + 8; ++i) {
            reached.insert(at);
            QTest::keyClick(at, Qt::Key_Tab);
            QApplication::processEvents();
            QWidget* next = QApplication::focusWidget();
            if (!next || next == first || next->window() != &dialog) break;
            at = next;
        }
    }
    const auto reachedSet = [&](const QWidget* w) {
        for (QWidget* r : std::as_const(reached))
            if (inExclusiveSet(r) && r->parentWidget() == w->parentWidget()) return true;
        return false;
    };
    for (QWidget* w : std::as_const(tabbable))
        if (!reached.contains(w) && !(inExclusiveSet(w) && reachedSet(w))) p.unreachable << describe(w);

    // Shortcut letters.
    QHash<QChar, QString> letters;
    const auto letter = [&](const QString& text, const QWidget* w) {
        const QChar c = mnemonic(text);
        if (c.isNull() || !w->isVisibleTo(&dialog)) return;
        if (letters.contains(c)) p.clashes << QStringLiteral("'%1': %2 / %3").arg(c, letters.value(c), text);
        else letters.insert(c, text);
    };
    for (QWidget* w : widgets) {
        if (auto* b = qobject_cast<QAbstractButton*>(w)) letter(b->text(), b);
        if (auto* l = qobject_cast<QLabel*>(w); l && l->buddy()) letter(l->text(), l);
    }

    // Enter.
    if (needsDefault) {
        bool found = false;
        for (auto* b : dialog.findChildren<QPushButton*>())
            if (b->isVisibleTo(&dialog) && (b->isDefault() || b->autoDefault())) found = true;
        p.hasDefault = found;
    }

    // Escape.
    QWidget* focus = QApplication::focusWidget();
    p.escapeFocus = focus ? describe(focus) : QStringLiteral("nothing");
    QTest::keyClick(focus && focus->window() == &dialog ? focus : static_cast<QWidget*>(&dialog), Qt::Key_Escape);
    QApplication::processEvents();
    // A first Escape may only put away a tip or popup.
    if (dialog.isVisible()) {
        focus = QApplication::focusWidget();
        QTest::keyClick(focus && focus->window() == &dialog ? focus : static_cast<QWidget*>(&dialog), Qt::Key_Escape);
        QApplication::processEvents();
    }
    p.escapeCloses = !dialog.isVisible();
    dialog.hide();
    return p;
}

parts::PartsLibrary& library() {
    static parts::PartsLibrary lib;
    static bool scanned = false;
    if (!scanned) {
        const QString root = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        if (QDir(root).exists()) {
            lib.addSearchPath(root);
            lib.scan();
        }
        scanned = true;
    }
    return lib;
}

std::unique_ptr<core::Map> sampleMap() {
    auto r = saveload::readBbm(QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm"));
    return r.ok() ? std::move(r.map) : std::make_unique<core::Map>();
}

class DialogKeyboard : public ::testing::Test {
protected:
    void SetUp() override { QStandardPaths::setTestModeEnabled(true); }
    void TearDown() override { QStandardPaths::setTestModeEnabled(false); }
    void check(const char* name, QDialog& d, bool needsDefault = true) {
        const Problems p = audit(d, needsDefault);
        EXPECT_TRUE(p.all().isEmpty()) << name << ":\n" << p.all().toStdString();
    }
};

}  // namespace

TEST_F(DialogKeyboard, Settings) {
    ui::theme::PrefsStore store(QStringLiteral("dialogKeyboardTest"));
    ui::SettingsDialog d(store, QString());
    check("Settings", d);
}

TEST_F(DialogKeyboard, MoreOptions) {
    ui::PreferencesDialog d;
    check("More options", d);
}

TEST_F(DialogKeyboard, Confirm) {
    ui::ConfirmDialog d(ui::ConfirmDialog::deleteOptions(QStringLiteral("Main yard"), ui::ConfirmDialog::setWording()));
    // Enter never deletes by itself: neither button is the default, on purpose.
    check("Confirm", d, false);
}

TEST_F(DialogKeyboard, PartsFolders) {
    ui::LibraryPathsDialog d({ QStringLiteral("/tmp/a") });
    check("Parts folders", d);
}

TEST_F(DialogKeyboard, GetMoreParts) {
    ui::DownloadCenterDialog d(QDir::tempPath());
    check("Get more parts", d);
}

TEST_F(DialogKeyboard, Find) {
    ui::MapView view(library());
    view.loadMap(sampleMap());
    ui::FindDialog d(view);
    check("Find", d);
}

TEST_F(DialogKeyboard, Budget) {
    auto map = sampleMap();
    ui::BudgetSession budget(library());
    ui::BudgetDialog d(*map, budget);
    check("Budget", d);
}

TEST_F(DialogKeyboard, SaveSet) {
    ui::SaveSetDialog d(4, QStringLiteral("Club server"), true);
    check("Save set", d);
    ui::RenameSetDialog r(QStringLiteral("Station"));
    check("Rename set", r);
    ui::SetNameTakenDialog t(QStringLiteral("Station"), QStringLiteral("Station 2"), QStringLiteral("Club server"));
    check("Set name taken", t);
}

TEST_F(DialogKeyboard, MakeModule) {
    ui::MakeModuleDialog d(nullptr, QString(), 12);
    check("Make module", d);
    ui::PublishModuleDialog p(QStringLiteral("Yard"), 2, 3, true);
    check("Publish module", p);
}

TEST_F(DialogKeyboard, Venue) {
    ui::VenueDialog d(std::nullopt);
    check("Venue", d);
    ui::VenueDimensionsDialog v;
    check("Venue dimensions", v);
    core::Venue venue;
    venue.name = QStringLiteral("Hall");
    ui::VenueDesignerDialog g(venue, QStringLiteral("Hall"), QStringLiteral("Save"), [](const core::Venue&) { return QString(); });
    // Enter finishes what's being drawn there, not the dialog.
    check("Venue designer", g, false);
}

TEST_F(DialogKeyboard, PartDifferences) {
    ui::PartDifferencesDialog d({});
    check("Part differences", d);
}

TEST_F(DialogKeyboard, SharePicture) {
    auto map = sampleMap();
    ui::SharePictureDialog::Input in;
    in.map = map.get();
    in.parts = &library();
    in.layoutTitle = QStringLiteral("Corner");
    ui::SharePictureDialog d(in);
    check("Share picture", d);
}

TEST_F(DialogKeyboard, Shortcuts) {
    ui::help::ShortcutsDialog d({});
    check("Shortcuts", d, false);
}

TEST_F(DialogKeyboard, Welcome) {
    ui::tours::WelcomeDialog d;
    // A choice of ways in; none is picked for you.
    check("Welcome", d, false);
}

// Every dialog the menus open (the ones ending in "…"), on a small layout
// with a part picked: the same checks, through the window as people use it.
TEST_F(DialogKeyboard, EveryMenuDialog) {
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    ui::MainWindow w(library());
    w.resize(1200, 800);
    w.show();
    ASSERT_TRUE(w.openFile(QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm")));
    if (auto* view = w.findChild<ui::MapView*>()) {
        view->selectAll();
    }
    QStringList report;
    QSet<QDialog*> seen;
    QString current;
    QTimer watch;
    watch.setInterval(30);
    QObject::connect(&watch, &QTimer::timeout, [&] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m) return;
        auto* d = qobject_cast<QDialog*>(m);
        if (!d) {
            m->close();
            return;
        }
        // Qt's own dialogs (files, colors, printing, questions) are Qt's to name.
        if (seen.contains(d) || qobject_cast<QFileDialog*>(d) || qobject_cast<QMessageBox*>(d)
            || qobject_cast<QInputDialog*>(d) || qobject_cast<QColorDialog*>(d) || d->inherits("QAbstractPrintDialog")) {
            d->reject();
            return;
        }
        seen.insert(d);
        watch.stop();
        const Problems p = audit(*d);
        if (!p.all().isEmpty()) report << current + QStringLiteral(" (") + d->windowTitle() + QStringLiteral("):\n") + p.all();
        if (d->isVisible()) d->reject();
        watch.start();
    });
    watch.start();
    const QStringList skip{ QStringLiteral("server"), QStringLiteral("Server"), QStringLiteral("Sign"),
                            QStringLiteral("Live"), QStringLiteral("live"), QStringLiteral("Get More"),
                            QStringLiteral("Open"), QStringLiteral("Import"), QStringLiteral("Export"),
                            QStringLiteral("Save"), QStringLiteral("Upload"), QStringLiteral("Check for") };
    for (QAction* top : w.menuBar()->actions()) {
        QMenu* menu = top->menu();
        if (!menu) continue;
        QList<QMenu*> menus{ menu };
        for (int i = 0; i < menus.size(); ++i) {
            for (QAction* a : menus[i]->actions()) {
                if (a->menu()) {
                    menus << a->menu();
                    continue;
                }
                const QString text = a->text();
                if (!(text.endsWith(QStringLiteral("...")) || text.endsWith(QChar(0x2026))) || !a->isEnabled()) continue;
                if (std::any_of(skip.cbegin(), skip.cend(), [&](const QString& k) { return text.contains(k); })) continue;
                current = top->text().remove(QLatin1Char('&')) + QStringLiteral(" > ") + QString(text).remove(QLatin1Char('&'));
                a->trigger();
                QApplication::processEvents();
                for (QWidget* t : QApplication::topLevelWidgets())
                    if (t != &w && t->isVisible() && !qobject_cast<QMenu*>(t)) t->close();
            }
        }
    }
    watch.stop();
    EXPECT_TRUE(report.isEmpty()) << report.join(QStringLiteral("\n\n")).toStdString();
}

