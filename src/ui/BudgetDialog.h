#pragma once

#include <QDialog>

class QLabel;
class QTableWidget;

namespace bld::core { class Map; }

namespace bld::ui {

class BudgetSession;

// Edits the current budget's limits: every part the map uses or the
// budget lists, with its count and limit (blank = no limit). Over-budget
// rows are red. New / Open / Save live in the Budget menu.
class BudgetDialog : public QDialog {
    Q_OBJECT
public:
    BudgetDialog(core::Map& map, BudgetSession& budget, QWidget* parent = nullptr);

private:
    void rebuildTable();

    core::Map& map_;
    BudgetSession& budget_;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    bool editing_ = false;
};

}
