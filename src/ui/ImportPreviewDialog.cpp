#include "ImportPreviewDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>

namespace bld::ui {

// QGraphicsView with built-in zoom support — Ctrl+wheel to zoom, plain
// wheel scrolls, zoom anchored under the cursor.
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

}  // namespace bld::ui

namespace bld::ui {

namespace {

QString connectionTypeName(const QString& type) {
    // BlueBrickParts' ConnectionTypeList.xml numbering.
    static const QStringList names{ QString(), ImportPreviewDialog::tr("Rail"),
        ImportPreviewDialog::tr("Road"), ImportPreviewDialog::tr("Monorail"),
        ImportPreviewDialog::tr("Monorail (short curve)") };
    bool ok = false;
    const int n = type.toInt(&ok);
    return ok && n > 0 && n < names.size() ? names[n] : ImportPreviewDialog::tr("Type %1").arg(type);
}

}  // namespace

ImportPreviewDialog::ImportPreviewDialog(PreparedPart part,
                                         const QStringList& categories,
                                         const QString& defaultCategory,
                                         std::function<bool(const QString&, const QString&)> partExists,
                                         QWidget* parent)
    : QDialog(parent), part_(std::move(part)), partExists_(std::move(partExists)) {
    setWindowTitle(tr("Preview: %1").arg(part_.kindLabel));
    resize(900, 640);
    auto* root = new QVBoxLayout(this);

    header_ = new QLabel(this);
    header_->setTextFormat(Qt::RichText);
    root->addWidget(header_);

    auto* split = new QSplitter(this);
    root->addWidget(split, 1);

    // Left: the sprite with its connection points.
    auto* left = new QWidget(split);
    auto* leftCol = new QVBoxLayout(left);
    leftCol->setContentsMargins(0, 0, 0, 0);
    scene_ = new QGraphicsScene(this);
    view_ = new PreviewView(scene_, left);
    view_->setMinimumSize(360, 300);
    leftCol->addWidget(view_, 1);
    auto* tools = new QHBoxLayout();
    auto* rotL = new QPushButton(tr("Rotate ⟲"), left);
    auto* rotR = new QPushButton(tr("Rotate ⟳"), left);
    rotL->setToolTip(tr("Rotate the part 90° counter-clockwise"));
    rotR->setToolTip(tr("Rotate the part 90° clockwise"));
    auto* zoomOut = new QPushButton(tr("−"), left);
    auto* zoomIn  = new QPushButton(tr("+"), left);
    auto* fitBtn  = new QPushButton(tr("Fit"), left);
    for (auto* b : { zoomOut, zoomIn }) b->setMaximumWidth(32);
    tools->addWidget(rotL);
    tools->addWidget(rotR);
    tools->addStretch();
    tools->addWidget(zoomOut);
    tools->addWidget(zoomIn);
    tools->addWidget(fitBtn);
    leftCol->addLayout(tools);
    connect(rotL, &QPushButton::clicked, this, [this]{ rotate(-1); });
    connect(rotR, &QPushButton::clicked, this, [this]{ rotate(1); });
    connect(zoomOut, &QPushButton::clicked, this, [this]{ view_->scaleBy(1.0 / 1.25); });
    connect(zoomIn,  &QPushButton::clicked, this, [this]{ view_->scaleBy(1.25); });
    connect(fitBtn,  &QPushButton::clicked, this, [this]{
        view_->resetTransform();
        view_->setCurrentScale(1.0);
        view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
        view_->setCurrentScale(view_->transform().m11());
    });

    // Right: connections, stats, warnings.
    auto* right = new QWidget(split);
    auto* rightCol = new QVBoxLayout(right);
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->addWidget(new QLabel(tr("Connection points (uncheck to drop):"), right));
    connList_ = new QListWidget(right);
    rightCol->addWidget(connList_, 1);
    for (const auto& c : part_.connections) {
        auto* item = new QListWidgetItem(connList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        Q_UNUSED(c);
    }
    if (part_.connections.isEmpty()) {
        auto* none = new QListWidgetItem(tr("None — the part won't snap to track."), connList_);
        none->setFlags(Qt::NoItemFlags);
    }
    connect(connList_, &QListWidget::itemChanged, this, [this]{ refreshSprite(); });
    connect(connList_, &QListWidget::currentRowChanged, this, [this]{ refreshSprite(); });

    auto* stats = new QFormLayout();
    if (part_.stats.ldrawResolved > 0)
        stats->addRow(tr("Parts rendered:"), new QLabel(QString::number(part_.stats.ldrawResolved), right));
    if (part_.stats.lddRendered > 0)
        stats->addRow(tr("LDD parts rendered:"), new QLabel(QString::number(part_.stats.lddRendered), right));
    if (part_.stats.unresolved > 0)
        stats->addRow(tr("Parts missing:"), new QLabel(QString::number(part_.stats.unresolved), right));
    rightCol->addLayout(stats);
    if (!part_.warnings.isEmpty()) {
        rightCol->addWidget(new QLabel(tr("%n warning(s):", nullptr, part_.warnings.size()), right));
        auto* warn = new QPlainTextEdit(right);
        warn->setReadOnly(true);
        warn->setMaximumHeight(110);
        warn->setPlainText(part_.warnings.join(QLatin1Char('\n')));
        rightCol->addWidget(warn);
    }
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    // Where it goes.
    auto* form = new QFormLayout();
    nameEdit_ = new QLineEdit(QFileInfo(part_.source).completeBaseName(), this);
    form->addRow(tr("Save as:"), nameEdit_);
    categoryBox_ = new QComboBox(this);
    categoryBox_->setEditable(true);
    categoryBox_->addItems(categories);
    categoryBox_->setCurrentText(defaultCategory);
    categoryBox_->setToolTip(tr("Parts panel category (a folder of your custom parts)"));
    form->addRow(tr("Category:"), categoryBox_);
    replaceBox_ = new QCheckBox(this);
    form->addRow(QString(), replaceBox_);
    root->addLayout(form);

    auto* bb = new QDialogButtonBox(this);
    auto* acceptBtn = bb->addButton(tr("Save as Custom Part"), QDialogButtonBox::AcceptRole);
    bb->addButton(QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const auto validate = [this, acceptBtn]{
        acceptBtn->setEnabled(!partName().isEmpty() && !category().isEmpty());
        refreshReplace();
    };
    connect(nameEdit_, &QLineEdit::textChanged, this, validate);
    connect(categoryBox_, &QComboBox::currentTextChanged, this, validate);
    root->addWidget(bb);

    refreshConnections();
    refreshSprite();
    validate();
    QMetaObject::invokeMethod(fitBtn, &QPushButton::click, Qt::QueuedConnection);
}

void ImportPreviewDialog::rotate(int quarterTurns) {
    rotatePart(part_, quarterTurns);
    refreshConnections();
    refreshSprite();
    view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
    view_->setCurrentScale(view_->transform().m11());
}

void ImportPreviewDialog::refreshConnections() {
    const QSignalBlocker block(connList_);
    for (int i = 0; i < part_.connections.size(); ++i) {
        const auto& c = part_.connections[i];
        connList_->item(i)->setText(tr("%1 at (%2, %3), facing %4°")
            .arg(connectionTypeName(c.type))
            .arg(c.xStuds, 0, 'f', 2).arg(c.yStuds, 0, 'f', 2)
            .arg(c.angleDeg, 0, 'f', 1));
    }
    header_->setText(tr("<b>%1</b> — %2 × %3 studs, %n connection point(s)", nullptr,
                        part_.connections.size())
                         .arg(QFileInfo(part_.source).fileName())
                         .arg(part_.widthStuds).arg(part_.heightStuds));
}

void ImportPreviewDialog::refreshSprite() {
    scene_->clear();
    pixmapItem_ = scene_->addPixmap(QPixmap::fromImage(part_.sprite));
    scene_->setSceneRect(pixmapItem_->boundingRect());
    const double pxX = part_.widthStuds  > 0 ? part_.sprite.width()  / double(part_.widthStuds)  : 8.0;
    const double pxY = part_.heightStuds > 0 ? part_.sprite.height() / double(part_.heightStuds) : 8.0;
    for (int i = 0; i < part_.connections.size(); ++i) {
        const auto& c = part_.connections[i];
        const bool on = connList_->item(i)->checkState() == Qt::Checked;
        const bool current = connList_->currentRow() == i;
        QPen pen(on ? (current ? QColor(255, 170, 0) : QColor(220, 30, 30)) : QColor(140, 140, 140));
        pen.setCosmetic(true);
        pen.setWidthF(current ? 3.0 : 2.0);
        if (!on) pen.setStyle(Qt::DashLine);
        const QPointF at((c.xStuds + part_.widthStuds / 2.0) * pxX,
                         (c.yStuds + part_.heightStuds / 2.0) * pxY);
        const double r = 0.6 * pxX;
        scene_->addEllipse(QRectF(at.x() - r, at.y() - r, 2 * r, 2 * r), pen);
        const double a = qDegreesToRadians(c.angleDeg);
        scene_->addLine(QLineF(at, at + QPointF(std::cos(a), std::sin(a)) * 2.0 * pxX), pen);
    }
}

void ImportPreviewDialog::refreshReplace() {
    const bool exists = partExists_ && !partName().isEmpty()
                        && partExists_(partName(), category());
    replaceBox_->setVisible(exists);
    replaceBox_->setText(tr("Replace the existing part “%1” in %2 (otherwise it is saved as a copy)")
                             .arg(partName(), category()));
}

PreparedPart ImportPreviewDialog::result() const {
    PreparedPart out = part_;
    out.connections.clear();
    for (int i = 0; i < part_.connections.size(); ++i) {
        const auto& c = part_.connections[i];
        if (connList_->item(i)->checkState() == Qt::Checked) out.connections.append(c);
        else out.droppedConnections.append(QPointF(c.xStuds, c.yStuds));
    }
    return out;
}

void ImportPreviewDialog::presetForReimport(const QString& name, const QString& category) {
    nameEdit_->setText(name);
    categoryBox_->setCurrentText(category);
    replaceBox_->setChecked(true);
    refreshReplace();
    setWindowTitle(tr("Re-import %1").arg(name));
}

QString ImportPreviewDialog::partName() const { return nameEdit_->text().trimmed(); }

QString ImportPreviewDialog::category() const { return categoryBox_->currentText().trimmed(); }

bool ImportPreviewDialog::replaceExisting() const {
    return !replaceBox_->isHidden() && replaceBox_->isChecked();
}

}  // namespace bld::ui
