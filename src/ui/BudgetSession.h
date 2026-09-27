#pragma once

#include "../edit/Budget.h"

#include <QObject>
#include <QString>

#include <optional>

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

// The budget the user is working with (BlueBrick's Budget.Instance): at
// most one, created or opened from the Budget menu, reopened at startup,
// and shared by the parts browser, the part-usage panel, the status bar
// and placement (when Use Budget Limitation is on).
class BudgetSession : public QObject {
    Q_OBJECT
public:
    explicit BudgetSession(parts::PartsLibrary& lib, QObject* parent = nullptr);

    bool exists() const { return budget_.has_value(); }
    const edit::Budget* budget() const { return budget_ ? &*budget_ : nullptr; }
    QString filePath() const { return path_; }
    // The name shown in the title bar ("Untitled.bbb" until saved).
    QString displayName() const;
    bool hasFileName() const { return !path_.isEmpty(); }
    bool isModified() const { return modified_; }

    void create();
    bool open(const QString& path, QString* error = nullptr);
    // BlueBrick's Import and Merge: the file's limits added to the current ones.
    bool importAndMerge(const QString& path, QString* error = nullptr);
    void close();
    bool saveAs(const QString& path, QString* error = nullptr);

    // Reopen the budget that was open when the app last closed, if any.
    void restoreFromSettings();

    // Per-part limit (-1 = unlimited). Setting a negative value clears it.
    int limit(const QString& part) const;
    void setLimit(const QString& part, int limit);
    // BlueBrick's IsBudgeted: a positive limit.
    bool isBudgeted(const QString& part) const { return exists() && limit(part) > 0; }

    // Budget menu options (remembered across sessions); only apply while a
    // budget exists.
    bool showOnlyBudgetedParts() const;
    bool showBudgetNumbers() const;
    bool useBudgetLimitation() const;
    void setShowOnlyBudgetedParts(bool on);
    void setShowBudgetNumbers(bool on);
    void setUseBudgetLimitation(bool on);
    // Preferences: parts without a limit are unlimited (true) or forbidden.
    static bool defaultBudgetIsInfinite();
    static void setDefaultBudgetIsInfinite(bool on);

    // Whether placing `quantity` of `part` (a set counts its parts too)
    // keeps within the budget. Always true without Use Budget Limitation.
    bool canAdd(const core::Map& map, const QString& part, int quantity = 1) const;

signals:
    void changed();

private:
    void setBudget(std::optional<edit::Budget> budget, const QString& path, bool modified);

    parts::PartsLibrary& lib_;
    std::optional<edit::Budget> budget_;
    QString path_;
    bool modified_ = false;
};

}  // namespace bld::ui
