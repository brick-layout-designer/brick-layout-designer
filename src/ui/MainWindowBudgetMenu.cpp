// Budget menu (BlueBrick's): New / Open / Import and Merge / Close / Save /
// Save As, the three budget options, and the budget editor.

#include "MainWindow.h"

#include "BudgetDialog.h"
#include "BudgetSession.h"
#include "MapView.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

namespace bld::ui {

namespace {

QString budgetFilter() { return QObject::tr("BlueBrick budget (*.bbb)"); }

}  // namespace

bool MainWindow::maybeSaveBudget() {
    if (!budget_->exists() || !budget_->isModified()) return true;
    const auto btn = QMessageBox::question(
        this, tr("Unsaved budget"),
        tr("The budget %1 has unsaved changes. Save it before continuing?").arg(budget_->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Save)    return saveBudget(false);
    if (btn == QMessageBox::Discard) return true;
    return false;
}

bool MainWindow::saveBudget(bool askForName) {
    QString path = budget_->filePath();
    if (askForName || path.isEmpty()) {
        const QString start = path.isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(budget_->displayName())
            : path;
        path = QFileDialog::getSaveFileName(this, tr("Save budget"), start, budgetFilter());
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".bbb");
    }
    QString err;
    if (!budget_->saveAs(path, &err)) {
        QMessageBox::warning(this, tr("Save budget"), err);
        return false;
    }
    return true;
}

void MainWindow::setupBudgetMenu() {
    auto* menu = menuBar()->addMenu(tr("&Budget"));

    auto* newAct = menu->addAction(tr("&New Budget"));
    connect(newAct, &QAction::triggered, this, [this] {
        if (!maybeSaveBudget()) return;
        budget_->create();
        statusBar()->showMessage(
            tr("New budget started. Budget › Edit Budget sets how many of each part you have."), 6000);
    });
    auto* openAct = menu->addAction(tr("&Open Budget..."));
    connect(openAct, &QAction::triggered, this, [this]{
        if (!maybeSaveBudget()) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Open budget"), budget_->filePath(), budgetFilter());
        if (path.isEmpty()) return;
        QString err;
        if (!budget_->open(path, &err)) QMessageBox::warning(this, tr("Open budget"), err);
    });
    auto* mergeAct = menu->addAction(tr("&Import and Merge Budget..."));
    mergeAct->setToolTip(tr("Add the limits of another budget file to the current budget"));
    connect(mergeAct, &QAction::triggered, this, [this]{
        const QString path = QFileDialog::getOpenFileName(this, tr("Import and merge budget"),
                                                          budget_->filePath(), budgetFilter());
        if (path.isEmpty()) return;
        QString err;
        if (!budget_->importAndMerge(path, &err)) QMessageBox::warning(this, tr("Import and merge budget"), err);
    });
    auto* closeAct = menu->addAction(tr("&Close Budget"));
    connect(closeAct, &QAction::triggered, this, [this]{
        if (maybeSaveBudget()) budget_->close();
    });
    menu->addSeparator();
    auto* saveAct = menu->addAction(tr("&Save Budget"));
    connect(saveAct, &QAction::triggered, this, [this]{ saveBudget(false); });
    auto* saveAsAct = menu->addAction(tr("Save Budget &As..."));
    connect(saveAsAct, &QAction::triggered, this, [this]{ saveBudget(true); });
    menu->addSeparator();

    auto* onlyAct = menu->addAction(tr("Show Only &Parts in the Budget"));
    onlyAct->setCheckable(true);
    onlyAct->setToolTip(tr("Hide parts without a budget from the Parts panel"));
    connect(onlyAct, &QAction::toggled, this, [this](bool on){ budget_->setShowOnlyBudgetedParts(on); });
    auto* numbersAct = menu->addAction(tr("Show Budget &Numbers"));
    numbersAct->setCheckable(true);
    numbersAct->setToolTip(tr("Show how many of each part are used and allowed in the Parts panel"));
    connect(numbersAct, &QAction::toggled, this, [this](bool on){ budget_->setShowBudgetNumbers(on); });
    auto* limitAct = menu->addAction(tr("Stop at the Budget &Limits"));
    limitAct->setCheckable(true);
    limitAct->setToolTip(tr("Don't let anyone place more of a part than its budget allows"));
    connect(limitAct, &QAction::toggled, this, [this, numbersAct](bool on){
        budget_->setUseBudgetLimitation(on);
        // As BlueBrick: offer to show the numbers, which make the limit visible.
        const QString key = QStringLiteral("general/askShowBudgetNumbers");
        if (!on || numbersAct->isChecked() || !QSettings().value(key, true).toBool()) return;
        QMessageBox box(QMessageBox::Question, tr("Budget limits"),
                        tr("Do you also want to show the budget numbers in the Parts panel?"),
                        QMessageBox::Yes | QMessageBox::No, this);
        auto* dontAsk = new QCheckBox(tr("Don't ask again"), &box);
        box.setCheckBox(dontAsk);
        const bool yes = box.exec() == QMessageBox::Yes;
        QSettings().setValue(key, !dontAsk->isChecked());
        if (yes) numbersAct->setChecked(true);
    });
    menu->addSeparator();
    auto* editAct = menu->addAction(tr("&Edit Budget..."));
    connect(editAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap() || !budget_->exists()) return;
        auto* dlg = new BudgetDialog(*mapView_->currentMap(), *budget_, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    const auto sync = [=, this]{
        const bool on = budget_->exists();
        for (QAction* a : { mergeAct, closeAct, saveAct, saveAsAct, onlyAct, numbersAct, limitAct, editAct })
            a->setEnabled(on);
        for (auto [a, v] : { std::pair{ onlyAct, budget_->showOnlyBudgetedParts() },
                             std::pair{ numbersAct, budget_->showBudgetNumbers() },
                             std::pair{ limitAct, budget_->useBudgetLimitation() } }) {
            const QSignalBlocker block(a);
            a->setChecked(v);
        }
        updateTitle();
    };
    connect(budget_, &BudgetSession::changed, this, sync);
    sync();
}

}  // namespace bld::ui
