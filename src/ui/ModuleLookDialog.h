#pragma once

// A placed module's look: whether its name shows, its outline and name
// colors, the "Same color" link and Reset to default (the web's
// ModuleLookDialog.tsx). Each change is applied at once (one undo step,
// and for everyone else on a live layout); the dialog follows the module
// as it changes.

#include "../core/Module.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QPushButton;

namespace bld::ui {

class ModuleLookDialog : public QDialog {
    Q_OBJECT
public:
    // `current` reads the module as it is now (null once it is gone);
    // `apply` writes a changed copy, with the undo text (and a merge key
    // for color picks).
    using Current = std::function<const core::Module*()>;
    using Apply = std::function<void(const core::Module& changed, const QString& undoText)>;
    ModuleLookDialog(Current current, Apply apply, QWidget* parent = nullptr);

    // Reads the module again (after any change); closes when it's gone.
    void refresh();
    // The module's own default color (what its pickers show when none is chosen).
    std::function<QString()> defaultColor;

    // For tests: the controls.
    QCheckBox* showNameBox() const { return showName_; }
    QCheckBox* sameColorBox() const { return same_; }
    QPushButton* outlineButton() const { return outline_; }
    QPushButton* nameButton() const { return name_; }
    QPushButton* resetButton() const { return reset_; }
    // Picks a color without the color window (what a pick in it does).
    void pickColor(bool outline, const QColor& c);

private:
    void chooseColor(bool outline);
    Current current_;
    Apply apply_;
    QLabel* title_ = nullptr;
    QCheckBox* showName_ = nullptr;
    QPushButton* outline_ = nullptr;
    QPushButton* name_ = nullptr;
    QCheckBox* same_ = nullptr;
    QPushButton* reset_ = nullptr;
    bool refreshing_ = false;
};

}  // namespace bld::ui
