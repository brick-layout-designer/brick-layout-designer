#pragma once

// "Where should these go?": asked when a module being added has sheets the
// layout doesn't have, as the web asks (apps/web/src/editor/SheetChoiceDialog.tsx).
// One row per such sheet, each going to the picked sheet (the default) or
// a new sheet with its own name. Place puts the module down; Cancel puts
// nothing down.

#include "../edit/ModuleSheets.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace bld::core { class Map; }

namespace bld::ui {

class SheetChoiceDialog : public QDialog {
    Q_OBJECT
public:
    SheetChoiceDialog(const core::Map& map, const QString& moduleName, int moduleSheets,
                      std::vector<edit::UnmatchedSheet> unmatched, QWidget* parent = nullptr);

    // Where each unmatched sheet goes: a layer guid, or empty for a new sheet by its name.
    QString targetFor(const QString& sheetName) const;

    // Tests: the choice for each unmatched sheet, the text, the buttons.
    QComboBox* choiceFor(const QString& sheetName) const;
    QString bodyText() const;
    QPushButton* placeButton() const { return place_; }
    QPushButton* cancelButton() const { return cancel_; }

    // Asks when the batches have sheets `map` lacks and fills in their
    // targetLayerGuid; true straight away when every sheet matches. False
    // on Cancel: put nothing down.
    static bool choose(QWidget* parent, const core::Map& map, const QString& moduleName,
                       std::vector<edit::ImportBbmAsModuleCommand::LayerBatch>& batches);

    // Saving a module to this computer (no server, so no Save dialog): with
    // parts on two or more sheets, says "This module uses 2 sheets: …" and
    // offers "Put everything on one sheet". False on Cancel.
    static bool askModuleSheets(QWidget* parent, core::Map& module);

private:
    std::vector<edit::UnmatchedSheet> unmatched_;
    std::vector<QComboBox*> choices_;
    QLabel* body_ = nullptr;
    QPushButton* place_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace bld::ui
