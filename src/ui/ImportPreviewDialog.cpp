#include "ImportPreviewDialog.h"

#include <QtMath>
#include <cmath>

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace bld::ui {

namespace {

// QGraphicsView subclass with built-in zoom support — Ctrl+wheel to
// zoom, plain wheel scrolls. Centered on cursor under the wheel
// pointer, which is the natural feel users expect from any viewer.
// Buttons on the toolbar drive scaleBy() too. Replaces the previous
// QScrollArea + QLabel approach which silently failed to repaint
// when the label was resized inside the scroll viewport.
class PreviewView : public QGraphicsView {
public:
    PreviewView(QGraphicsScene* scene, QWidget* parent)
        : QGraphicsView(scene, parent) {
        setRenderHint(QPainter::SmoothPixmapTransform);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
        setAlignment(Qt::AlignCenter);
        // Checker pattern via background brush so transparency in the
        // sprite is visually obvious.
        QPixmap checker(16, 16);
        checker.fill(Qt::white);
        QPainter p(&checker);
        p.fillRect(0, 0, 8, 8, QColor(220, 220, 220));
        p.fillRect(8, 8, 8, 8, QColor(220, 220, 220));
        p.end();
        setBackgroundBrush(QBrush(checker));
    }

    void scaleBy(double factor) {
        const double next = currentScale_ * factor;
        if (next < 0.05 || next > 32.0) return;
        currentScale_ = next;
        scale(factor, factor);
    }

    double currentScale() const { return currentScale_; }

    void setCurrentScale(double s) {
        if (s <= 0) return;
        const double factor = s / currentScale_;
        scaleBy(factor);
    }

protected:
    void wheelEvent(QWheelEvent* e) override {
        if (e->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = e->angleDelta().y();
            if (delta != 0) scaleBy(delta > 0 ? 1.15 : 1.0 / 1.15);
            e->accept();
            return;
        }
        QGraphicsView::wheelEvent(e);
    }

private:
    double currentScale_ = 1.0;
};

}  // namespace

ImportPreviewDialog::ImportPreviewDialog(const QString& sourceFile,
                                          const QString& kindLabel,
                                          const QImage& sprite,
                                          int widthStuds,
                                          int heightStuds,
                                          const Stats& stats,
                                          const QStringList& errors,
                                          const QVector<import::ImportedConnection>& connections,
                                          QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Preview: %1").arg(kindLabel));
    resize(720, 620);

    auto* root = new QVBoxLayout(this);

    // Header.
    {
        const QString name = QFileInfo(sourceFile).fileName();
        auto* hdr = new QLabel(
            tr("<b>%1</b> — %2 × %3 studs").arg(name).arg(widthStuds).arg(heightStuds), this);
        hdr->setTextFormat(Qt::RichText);
        root->addWidget(hdr);
    }

    // Scene + view. Adding a QGraphicsPixmapItem and setting the
    // scene rect to the pixmap bounds gives us a known canvas the
    // user can pan around within when zoomed in.
    auto* scene = new QGraphicsScene(this);
    QGraphicsPixmapItem* pmItem = nullptr;
    if (!sprite.isNull()) {
        pmItem = scene->addPixmap(QPixmap::fromImage(sprite));
        scene->setSceneRect(pmItem->boundingRect());

        // Mark each connection the part will snap by: a ring at the
        // point plus a tick in the direction it faces.
        const double pxPerStudX = widthStuds  > 0 ? sprite.width()  / double(widthStuds)  : 8.0;
        const double pxPerStudY = heightStuds > 0 ? sprite.height() / double(heightStuds) : 8.0;
        QPen pen(QColor(220, 30, 30));
        pen.setCosmetic(true);
        pen.setWidthF(2.0);
        for (const auto& c : connections) {
            const QPointF at((c.xStuds + widthStuds / 2.0) * pxPerStudX,
                             (c.yStuds + heightStuds / 2.0) * pxPerStudY);
            const double r = 0.6 * pxPerStudX;
            scene->addEllipse(QRectF(at.x() - r, at.y() - r, 2 * r, 2 * r), pen);
            const double a = qDegreesToRadians(c.angleDeg);
            scene->addLine(QLineF(at, at + QPointF(std::cos(a), std::sin(a)) * 2.0 * pxPerStudX), pen);
        }
    }
    auto* view = new PreviewView(scene, this);
    view->setMinimumHeight(320);
    root->addWidget(view, 1);

    // Zoom toolbar BELOW the view (more discoverable than above; users
    // expect zoom controls grouped with the dialog buttons).
    auto* zoomRow = new QHBoxLayout();
    auto* zoomLabel = new QLabel(QStringLiteral("100%"), this);
    zoomLabel->setMinimumWidth(56);
    zoomLabel->setAlignment(Qt::AlignCenter);
    auto* zoomOut = new QPushButton(tr("Zoom out"), this);
    auto* zoomIn  = new QPushButton(tr("Zoom in"),  this);
    auto* fitBtn  = new QPushButton(tr("Fit"),  this);
    auto* native  = new QPushButton(tr("100%"), this);
    zoomRow->addWidget(zoomOut);
    zoomRow->addWidget(zoomIn);
    zoomRow->addWidget(zoomLabel);
    zoomRow->addStretch();
    zoomRow->addWidget(fitBtn);
    zoomRow->addWidget(native);
    root->addLayout(zoomRow);

    auto refreshLabel = [view, zoomLabel]{
        zoomLabel->setText(QStringLiteral("%1%").arg(int(std::round(view->currentScale() * 100))));
    };
    connect(zoomOut, &QPushButton::clicked, this, [view, refreshLabel]{
        view->scaleBy(1.0 / 1.25); refreshLabel();
    });
    connect(zoomIn, &QPushButton::clicked, this, [view, refreshLabel]{
        view->scaleBy(1.25); refreshLabel();
    });
    connect(fitBtn, &QPushButton::clicked, this, [view, refreshLabel, pmItem]{
        if (!pmItem) return;
        view->resetTransform();
        view->setCurrentScale(1.0);
        view->fitInView(pmItem, Qt::KeepAspectRatio);
        // After fitInView, the transform's m11 is the new scale.
        const double s = view->transform().m11();
        view->setCurrentScale(s);
        refreshLabel();
    });
    connect(native, &QPushButton::clicked, this, [view, refreshLabel]{
        view->resetTransform();
        view->setCurrentScale(1.0);
        refreshLabel();
    });

    // Initial fit so big sprites don't overwhelm the dialog and tiny
    // sprites get scaled up enough to see. Done in a single-shot so
    // the view has its real size by the time fit runs.
    if (pmItem) {
        QMetaObject::invokeMethod(this, [view, pmItem, refreshLabel, sprite]{
            // Tiny sprite (< 64 px each side) → scale to ~256 so the
            // user can actually see it. Otherwise fit to viewport.
            if (sprite.width() < 64 && sprite.height() < 64) {
                view->resetTransform();
                view->setCurrentScale(1.0);
                view->scaleBy(4.0);
            } else {
                view->resetTransform();
                view->setCurrentScale(1.0);
                view->fitInView(pmItem, Qt::KeepAspectRatio);
                view->setCurrentScale(view->transform().m11());
            }
            refreshLabel();
        }, Qt::QueuedConnection);
    }

    // Stats.
    {
        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        if (stats.ldrawResolved > 0)
            form->addRow(tr("LDraw-rendered:"), new QLabel(QString::number(stats.ldrawResolved), this));
        if (stats.lddRendered > 0)
            form->addRow(tr("LDD-rendered:"),   new QLabel(QString::number(stats.lddRendered), this));
        if (stats.translated > 0)
            form->addRow(tr("LDD→LDraw mapped:"), new QLabel(QString::number(stats.translated), this));
        if (stats.unmapped > 0)
            form->addRow(tr("Unmapped refs:"),  new QLabel(QString::number(stats.unmapped), this));
        if (stats.skipped > 0)
            form->addRow(tr("Skipped (no .g):"), new QLabel(QString::number(stats.skipped), this));
        form->addRow(tr("Connection points:"), new QLabel(connections.isEmpty()
            ? tr("none (the part won't snap to track)")
            : QString::number(connections.size()), this));
        root->addLayout(form);
    }

    // Errors panel.
    if (!errors.isEmpty()) {
        auto* errLabel = new QLabel(tr("%1 warning(s)/error(s) during bake:").arg(errors.size()), this);
        root->addWidget(errLabel);
        auto* errEdit = new QPlainTextEdit(this);
        errEdit->setReadOnly(true);
        errEdit->setMaximumHeight(120);
        errEdit->setPlainText(errors.join(QChar('\n')));
        root->addWidget(errEdit);
    }

    // Name field.
    {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Save as:"), this));
        nameEdit_ = new QLineEdit(this);
        nameEdit_->setText(QFileInfo(sourceFile).completeBaseName());
        row->addWidget(nameEdit_, 1);
        root->addLayout(row);
    }
    {
        auto* bb = new QDialogButtonBox(this);
        auto* acceptBtn = bb->addButton(tr("Save as Library Part"), QDialogButtonBox::AcceptRole);
        bb->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(nameEdit_, &QLineEdit::textChanged, this,
                [acceptBtn](const QString& s){ acceptBtn->setEnabled(!s.trimmed().isEmpty()); });
        acceptBtn->setEnabled(!nameEdit_->text().trimmed().isEmpty());
        root->addWidget(bb);
    }
}

QString ImportPreviewDialog::partName() const {
    return nameEdit_ ? nameEdit_->text().trimmed() : QString();
}

}  // namespace bld::ui
