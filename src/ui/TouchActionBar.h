#pragma once

// The bar that floats over the bottom of the map in touch mode, with the
// web's words. Something selected: Rotate left, Rotate right, Duplicate,
// Module, Delete and Done. Nothing selected: Add part, Undo and Redo. Placing a
// part picked in the parts panel: what to do, and Cancel.

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QToolButton;

namespace bld::ui {

class TouchActionBar : public QWidget {
    Q_OBJECT
public:
    explicit TouchActionBar(QWidget* parent);

    enum class Mode { Idle, Selection, Placing };
    void setMode(Mode mode, const QString& placingName = {});
    Mode mode() const { return mode_; }
    // Keeps the bar centred along the bottom of `area` (parent coords).
    void place(const QRect& area);

    QToolButton* button(const QString& objectName) const;

signals:
    void rotateLeft();
    void rotateRight();
    void duplicate();
    // The picked parts' module: its menu, or Make a module.
    void module();
    void remove();
    void done();
    void addPart();
    void undo();
    void redo();
    void cancelPlacing();

protected:
    void paintEvent(QPaintEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    QToolButton* addButton(const QString& name, const QString& text, const QString& icon);
    void refreshIcons();
    Mode mode_ = Mode::Idle;
    QHBoxLayout* row_ = nullptr;
    QLabel* placing_ = nullptr;
    QList<QToolButton*> selectionButtons_, idleButtons_, placingButtons_;
};

}  // namespace bld::ui
