#pragma once

// The part list (BlueBrick's PartUsageView) and its export as text, CSV
// or HTML, laid out like BlueBrick's.

#include <QImage>
#include <QString>

#include <functional>
#include <vector>

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

class Budget;

struct PartListRow {
    QString partNumber;   // full, e.g. "3811.1"
    QString part;         // without the colour, e.g. "3811"
    QString colorName;    // e.g. "Blue"
    QString description;
    int count = 0;
    // Budget columns, only with a budget: -1 = unbudgeted (unlimited).
    int budget = -1;
    int missing = 0;
    double usage = -1.0;  // percent; -1 = unbudgeted
};

struct PartListGroup {
    QString name;         // layer name, or empty for the whole map
    std::vector<PartListRow> rows;
    PartListRow total;
};

struct PartListOptions {
    bool splitPerLayer = false;
    bool includeHiddenLayers = true;
    const Budget* budget = nullptr;
    bool defaultBudgetIsInfinite = true;
    QString language = QStringLiteral("en");
};

std::vector<PartListGroup> buildPartList(const core::Map& map, const parts::PartsLibrary& lib,
                                         const PartListOptions& options);

// The exported file's contents. `title` is the heading ("Part List for
// file ..."); the budget columns read N/A without a budget. The HTML
// embeds each part's picture from `picture` (empty image: none).
QString partListText(const core::Map& map, const std::vector<PartListGroup>& groups,
                     const QString& title, bool hasBudget);
QString partListCsv(const std::vector<PartListGroup>& groups, bool hasBudget);
QString partListHtml(const core::Map& map, const std::vector<PartListGroup>& groups,
                     const QString& title, bool hasBudget,
                     const std::function<QImage(const QString& partNumber)>& picture);

// BlueBrick's percentage bar: ten block characters and " 45%".
QString percentageBar(double percent);

}  // namespace bld::edit
