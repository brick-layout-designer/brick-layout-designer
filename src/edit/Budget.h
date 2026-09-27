#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

// A BlueBrick budget (.bbb): how many of each part the layout may use.
// Parts without an entry are unlimited or forbidden depending on the
// "default budget is infinite" preference (BlueBrick's
// IsDefaultBudgetInfinite). Part ids compare case-insensitively; entries
// keep their file order so a budget saves back as BlueBrick wrote it.
class Budget {
public:
    // BlueBrick's XmlSerializer format:
    //   <Budget><Version>1</Version><PartList><Part id="3001.1">12</Part>...
    // Old part names map to their current ones when `lib` is given.
    // Nothing on a missing or malformed file.
    static std::optional<Budget> read(const QString& path, const parts::PartsLibrary* lib = nullptr);
    bool write(const QString& path) const;

    bool isEmpty() const { return entries_.isEmpty(); }
    const QList<QPair<QString, int>>& entries() const { return entries_; }
    bool hasLimit(const QString& part) const { return index_.contains(part.toUpper()); }
    // The limit for `part`, or -1 (unlimited) / 0 (forbidden) when unset.
    int limit(const QString& part, bool defaultInfinite = true) const;
    // A negative limit removes the entry. Returns whether anything changed.
    bool setLimit(const QString& part, int limit);
    // BlueBrick's mergeWith: limits of parts in both budgets add up.
    void mergeWith(const Budget& other);

private:
    void reindex();
    QList<QPair<QString, int>> entries_;
    QHash<QString, int> index_;  // upper-cased id -> position in entries_
};

// Count every brick by part number (upper-cased) across the brick layers,
// optionally skipping hidden layers.
QHash<QString, int> countPartUsage(const core::Map& map, bool includeHiddenLayers = true);

// One part over its limit. `overBy` is `used - limit` (always > 0).
struct BudgetViolation {
    QString partNumber;
    int     used = 0;
    int     limit = 0;
    int     overBy = 0;
};

// Every budgeted part the map uses more of than its limit.
QVector<BudgetViolation> checkBudget(const core::Map& map, const Budget& budget);

// BlueBrick's Budget.canAddBrick: whether `quantity` more of `part` (and
// for a set, each of its leaf parts) fits the budget. `subparts` returns
// the leaf parts of a set with their counts (empty for a plain part).
bool canAddToBudget(const Budget& budget, const QHash<QString, int>& usage, const QString& part,
                    int quantity, bool defaultInfinite,
                    const std::function<QHash<QString, int>(const QString&)>& subparts);

// The leaf parts of a set in `lib`, with their counts (nested sets
// expanded); empty for a plain part.
QHash<QString, int> setLeafParts(const parts::PartsLibrary& lib, const QString& part);

}  // namespace bld::edit
