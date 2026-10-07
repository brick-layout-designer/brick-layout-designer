#pragma once

// The Venue Designer (the twin of the web's VenueDesigner.tsx): a large
// window for one venue. Tools on the left (one-key shortcuts), the drawing
// in the middle, the inspector on the right, layer toggles above, and the
// cursor, room size and hint below. It edits a copy; saveRequested(venue)
// hands the result back (the layout's venue, or a library file).

#include "edit/venue/VenueDesignerState.h"

#include <QDialog>
#include <QImage>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace bld::ui {

class VenueDesignerView;

// The floor plan kept in the venue's `floorPlan` field (VENUE-MODEL.md).
struct FloorPlan {
    QImage image;
    QPointF topLeft; // studs
    double studsPerPx = 1.0;
    double opacity = 0.4;
};
std::optional<FloorPlan> floorPlanOf(const core::Venue& v);
core::Venue withFloorPlan(core::Venue v, const std::optional<FloorPlan>& plan);
// Rescale so world points a and b are `realStuds` apart, keeping `a` in place.
FloorPlan calibratePlan(FloorPlan plan, QPointF a, QPointF b, double realStuds);

class VenueDesignerDialog : public QDialog {
    Q_OBJECT
public:
    // `save` stores the venue; it returns an error message, or empty on success.
    VenueDesignerDialog(core::Venue initial, const QString& subtitle, const QString& saveLabel,
                        std::function<QString(const core::Venue&)> save, QWidget* parent = nullptr);

    const core::Venue& venue() const { return state_.venue(); }
    const edit::venue::DesignerState& state() const { return state_; }
    bool dirty() const;
    void dispatch(const edit::venue::Action& a);
    bool saveNow();
    // "Save to Server…" in the header: a copy of the venue as it is now goes to a server.
    void setSendToServer(std::function<void(const core::Venue&)> send);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void reject() override;

private:
    void refresh();
    void rebuildInspector();
    void loadPlan();

    edit::venue::DesignerState state_;
    core::Venue saved_;
    std::function<QString(const core::Venue&)> save_;
    VenueDesignerView* view_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* cursor_ = nullptr;
    QLabel* size_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* estimates_ = nullptr;
    QComboBox* unit_ = nullptr;
    QCheckBox* snap_ = nullptr;
    QPushButton* undo_ = nullptr;
    QPushButton* redo_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    std::function<void(const core::Venue&)> send_;
    QList<QToolButton*> toolButtons_;
    QWidget* inspector_ = nullptr;
    QVBoxLayout* inspectorLayout_ = nullptr;
    std::optional<edit::venue::Selection> inspected_;
    int inspectedVersion_ = -1;
    int version_ = 0;
    bool planMoving_ = false;
    std::optional<FloorPlan> planCache_;
    QString planCacheKey_;
};

} // namespace bld::ui
