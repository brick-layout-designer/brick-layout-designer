#pragma once

// The Venue Designer's drawing area (the twin of the web's
// DesignerCanvas.tsx): the floor plan, a feet grid, the venue drawn by the
// SceneBuilder (so it looks as it does in a layout), the selection and its
// corner handles, and the shape being drawn with its live length. Mouse
// events become world points (studs); the wheel zooms at the pointer;
// middle or right drag pans.

#include "edit/venue/VenueDesignerState.h"

#include <QGraphicsView>
#include <QImage>

#include <memory>

namespace bld::parts {
class PartsLibrary;
}
namespace bld::rendering {
class SceneBuilder;
}

namespace bld::ui {

class VenueDesignerView : public QGraphicsView {
    Q_OBJECT
public:
    explicit VenueDesignerView(QWidget* parent = nullptr);
    ~VenueDesignerView() override;

    void setState(const edit::venue::DesignerState& state, const QImage& plan, QRectF planRectStuds,
                  double planOpacity);
    // Show the whole venue (or a 60 ft square when empty).
    void fit();
    // How close the pointer must be to pick or snap, in studs at this zoom.
    double tolStuds() const;
    // Studs per screen pixel's inverse: screen pixels per stud.
    double pixelsPerStud() const;
    void zoomBy(double factor);
    // Dragging moves the floor plan instead of using the tool.
    void setPlanMoving(bool on) { planMoving_ = on; }

signals:
    void pressed(QPointF studs, bool free);
    void moved(QPointF studs, bool free);
    void released();
    void cursorMoved(std::optional<QPointF> studs);
    void planDragged(QPointF deltaStuds);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    QPointF toStuds(QPoint viewPos) const;
    void redraw();

    struct Last {
        edit::venue::DesignerState state;
        QImage plan;
        QRectF planRect;
        double planOpacity;
    };
    std::optional<Last> last_;
    bool redrawing_ = false;

    QGraphicsScene scene_;
    std::unique_ptr<parts::PartsLibrary> parts_;
    std::unique_ptr<rendering::SceneBuilder> builder_;
    bool fitted_ = false;
    std::optional<QPoint> panFrom_;
    std::optional<QPointF> planFrom_;
    bool planMoving_ = false;
    QRectF venueBounds_;
};

} // namespace bld::ui
