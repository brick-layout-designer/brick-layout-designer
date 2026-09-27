#include "BudgetSession.h"

#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QFileInfo>
#include <QSettings>

namespace bld::ui {

namespace {

const QString kLastFile      = QStringLiteral("budget/lastFile");
const QString kOnlyBudgeted  = QStringLiteral("budget/showOnlyBudgetedParts");
const QString kNumbers       = QStringLiteral("budget/showBudgetNumbers");
const QString kLimitation    = QStringLiteral("budget/useBudgetLimitation");
const QString kDefaultInfinite = QStringLiteral("budget/defaultInfinite");

}  // namespace

BudgetSession::BudgetSession(parts::PartsLibrary& lib, QObject* parent) : QObject(parent), lib_(lib) {}

QString BudgetSession::displayName() const {
    return path_.isEmpty() ? tr("Untitled.bbb") : QFileInfo(path_).fileName();
}

void BudgetSession::setBudget(std::optional<edit::Budget> budget, const QString& path, bool modified) {
    budget_ = std::move(budget);
    path_ = path;
    modified_ = modified;
    emit changed();
}

void BudgetSession::create() {
    setBudget(edit::Budget{}, QString(), false);
}

bool BudgetSession::open(const QString& path, QString* error) {
    auto loaded = edit::Budget::read(path, &lib_);
    if (!loaded) {
        if (error) *error = tr("%1 is not a BlueBrick budget file.").arg(QFileInfo(path).fileName());
        return false;
    }
    QSettings().setValue(kLastFile, path);
    setBudget(std::move(loaded), path, false);
    return true;
}

bool BudgetSession::importAndMerge(const QString& path, QString* error) {
    auto loaded = edit::Budget::read(path, &lib_);
    if (!loaded) {
        if (error) *error = tr("%1 is not a BlueBrick budget file.").arg(QFileInfo(path).fileName());
        return false;
    }
    // As BlueBrick: the imported budget, merged with the current one, under
    // the current budget's name.
    const bool hadEntries = budget_ && !budget_->isEmpty();
    if (budget_) loaded->mergeWith(*budget_);
    setBudget(std::move(loaded), path_, modified_ || hadEntries);
    return true;
}

void BudgetSession::close() {
    QSettings().remove(kLastFile);
    setBudget(std::nullopt, QString(), false);
}

bool BudgetSession::saveAs(const QString& path, QString* error) {
    if (!budget_) return false;
    if (!budget_->write(path)) {
        if (error) *error = tr("Cannot save the budget to %1.").arg(path);
        return false;
    }
    QSettings().setValue(kLastFile, path);
    path_ = path;
    modified_ = false;
    emit changed();
    return true;
}

void BudgetSession::restoreFromSettings() {
    const QString last = QSettings().value(kLastFile).toString();
    if (last.isEmpty()) return;
    if (!open(last)) QSettings().remove(kLastFile);
}

int BudgetSession::limit(const QString& part) const {
    return budget_ ? budget_->limit(part, defaultBudgetIsInfinite()) : -1;
}

void BudgetSession::setLimit(const QString& part, int limit) {
    if (!budget_) return;
    if (budget_->setLimit(part, limit)) {
        modified_ = true;
        emit changed();
    }
}

bool BudgetSession::showOnlyBudgetedParts() const { return QSettings().value(kOnlyBudgeted, false).toBool(); }
bool BudgetSession::showBudgetNumbers() const     { return QSettings().value(kNumbers, false).toBool(); }
bool BudgetSession::useBudgetLimitation() const   { return QSettings().value(kLimitation, false).toBool(); }

void BudgetSession::setShowOnlyBudgetedParts(bool on) { QSettings().setValue(kOnlyBudgeted, on); emit changed(); }
void BudgetSession::setShowBudgetNumbers(bool on)     { QSettings().setValue(kNumbers, on); emit changed(); }
void BudgetSession::setUseBudgetLimitation(bool on)   { QSettings().setValue(kLimitation, on); emit changed(); }

bool BudgetSession::defaultBudgetIsInfinite() { return QSettings().value(kDefaultInfinite, true).toBool(); }
void BudgetSession::setDefaultBudgetIsInfinite(bool on) { QSettings().setValue(kDefaultInfinite, on); }

bool BudgetSession::canAdd(const core::Map& map, const QString& part, int quantity) const {
    if (!budget_ || !useBudgetLimitation()) return true;
    return edit::canAddToBudget(*budget_, edit::countPartUsage(map), part, quantity, defaultBudgetIsInfinite(),
                                [this](const QString& p) { return edit::setLeafParts(lib_, p); });
}

}  // namespace bld::ui
