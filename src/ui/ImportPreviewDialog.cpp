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
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QSignalBlocker>

#include <functional>
#include <tuple>
#include <QtMath>

#include <cmath>

namespace bld::ui {

// QGraphicsView with built-in zoom support — Ctrl+wheel to zoom, plain
// wheel scrolls, zoom anchored under the cursor.
class PreviewView : public QGraphicsView {
public:
    // Arrow keys: move the model one nudge step against the grid.
    std::function<void(QPointF)> onNudge;
    std::function<double()> nudgeStep;
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

    // The user zooming: the view stops following its size.
    void zoomBy(double factor) {
        const double next = currentScale_ * factor;
        if (next < 0.05 || next > 32.0) return;
        autoFit_ = false;
        currentScale_ = next;
        scale(factor, factor);
    }

    // Fit the whole model, centred, and keep it fitted as the view is
    // resized until the user zooms. Fitting once in the constructor ran
    // before the dialog had its size, so the model only fitted after a
    // rotate.
    void fitAll() {
        autoFit_ = true;
        refit();
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QGraphicsView::resizeEvent(e);
        if (autoFit_) refit();
    }
    void showEvent(QShowEvent* e) override {
        QGraphicsView::showEvent(e);
        if (autoFit_) refit();
    }
    void keyPressEvent(QKeyEvent* e) override {
        const double q = nudgeStep ? nudgeStep() : 0.25;
        QPointF d;
        switch (e->key()) {
        case Qt::Key_Left: d = { -q, 0 }; break;
        case Qt::Key_Right: d = { q, 0 }; break;
        case Qt::Key_Up: d = { 0, -q }; break;
        case Qt::Key_Down: d = { 0, q }; break;
        default: QGraphicsView::keyPressEvent(e); return;
        }
        if (onNudge) onNudge(d);
        e->accept();
    }
    // The scroll wheel zooms, towards the point under the pointer (the
    // preview has nothing else to scroll).
    void wheelEvent(QWheelEvent* e) override {
        const int delta = e->angleDelta().y();
        if (delta != 0) {
            const ViewportAnchor before = transformationAnchor();
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            zoomBy(delta > 0 ? 1.15 : 1.0 / 1.15);
            setTransformationAnchor(before);
        }
        e->accept();
    }

private:
    void refit() {
        if (!scene() || scene()->sceneRect().isEmpty()) return;
        resetTransform();
        fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
        currentScale_ = transform().m11();
    }

    double currentScale_ = 1.0;
    bool autoFit_ = true;
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

namespace {
// The last alignment chosen, for this session only.
ImportAlign gLastAlign = ImportAlign::Automatic;
// The nudge step chosen last time, so a run of imports keeps it.
double gLastNudgeStep = 0.25;
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
    auto* zoomOut = new QPushButton(tr("Zoom out"), left);
    auto* zoomIn  = new QPushButton(tr("Zoom in"), left);
    auto* fitBtn  = new QPushButton(tr("Fit"), left);
    zoomOut->setToolTip(tr("Zoom out (or scroll down over the picture)"));
    zoomIn->setToolTip(tr("Zoom in (or scroll up over the picture)"));
    fitBtn->setToolTip(tr("Show the whole model"));
    tools->addWidget(rotL);
    tools->addWidget(rotR);
    tools->addStretch();
    tools->addWidget(zoomOut);
    tools->addWidget(zoomIn);
    tools->addWidget(fitBtn);
    leftCol->addLayout(tools);

    // Stud alignment: where the stud grid falls on the model.
    auto* align = new QHBoxLayout();
    align->addWidget(new QLabel(tr("Align to:"), left));
    alignBox_ = new QComboBox(left);
    alignBox_->setObjectName(QStringLiteral("alignTo"));
    alignBox_->addItem(tr("Automatic"), static_cast<int>(ImportAlign::Automatic));
    alignBox_->addItem(tr("Bottom plates"), static_cast<int>(ImportAlign::BottomLayer));
    alignBox_->addItem(tr("Bounding box"), static_cast<int>(ImportAlign::BoundingBox));
    alignBox_->setToolTip(tr("Which studs go on the grid: Automatic puts the flat bottom plates on it, "
                             "else centres the model. Arrow keys in the picture move it by the step you pick."));
    align->addWidget(alignBox_);
    auto* nudgeLabel = new QLabel(tr("Move by"), left);
    nudgeLabel->setToolTip(tr("Shift the model against the stud grid by this much per click. "
                              "The arrow keys do the same while the picture has focus."));
    align->addWidget(nudgeLabel);
    stepBox_ = new QComboBox(left);
    stepBox_->setObjectName(QStringLiteral("nudgeStep"));
    stepBox_->addItem(tr("½ stud"), 0.5);
    stepBox_->addItem(tr("¼ stud"), 0.25);
    stepBox_->addItem(tr("⅛ stud"), 0.125);
    stepBox_->addItem(tr("1/16 stud"), 0.0625);
    stepBox_->setToolTip(nudgeLabel->toolTip());
    stepBox_->setCurrentIndex(std::max(0, stepBox_->findData(gLastNudgeStep)));
    connect(stepBox_, &QComboBox::currentIndexChanged, this,
            [this] { gLastNudgeStep = stepBox_->currentData().toDouble(); });
    align->addWidget(stepBox_);
    for (const auto& [text, d, name] : { std::tuple{ tr("Left"), QPointF(-1, 0), "nudgeLeft" },
                                         std::tuple{ tr("Right"), QPointF(1, 0), "nudgeRight" },
                                         std::tuple{ tr("Up"), QPointF(0, -1), "nudgeUp" },
                                         std::tuple{ tr("Down"), QPointF(0, 1), "nudgeDown" } }) {
        auto* b = new QPushButton(text, left);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(tr("Move the model %1 against the grid").arg(text.toLower()));
        const QPointF dir = d;
        connect(b, &QPushButton::clicked, this, [this, dir] { nudge(dir * nudgeStep()); });
        align->addWidget(b);
    }
    auto* reset = new QPushButton(tr("Reset"), left);
    reset->setToolTip(tr("Back to Automatic, with no nudge"));
    connect(reset, &QPushButton::clicked, this, [this] { nudge_ = {}; setAlign(ImportAlign::Automatic); });
    align->addWidget(reset);
    align->addStretch();
    leftCol->addLayout(align);
    connect(alignBox_, &QComboBox::currentIndexChanged, this, [this] {
        setAlign(static_cast<ImportAlign>(alignBox_->currentData().toInt()));
    });
    view_->onNudge = [this](QPointF d) { nudge(d); };
    view_->nudgeStep = [this] { return nudgeStep(); };
    connect(rotL, &QPushButton::clicked, this, [this]{ rotate(-1); });
    connect(rotR, &QPushButton::clicked, this, [this]{ rotate(1); });
    connect(zoomOut, &QPushButton::clicked, this, [this]{ view_->zoomBy(1.0 / 1.25); });
    connect(zoomIn,  &QPushButton::clicked, this, [this]{ view_->zoomBy(1.25); });
    connect(fitBtn,  &QPushButton::clicked, this, [this]{ view_->fitAll(); });

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
    // The model's own name, else its file's: what the part will be called.
    QString suggested = part_.title;
    if (suggested.isEmpty() || suggested.compare(QStringLiteral("Untitled Model"), Qt::CaseInsensitive) == 0
        || suggested.size() > 80)
        suggested = QFileInfo(part_.source).completeBaseName();
    nameEdit_ = new QLineEdit(suggested, this);
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

    base_ = part_;
    if (gLastAlign != ImportAlign::Automatic) setAlign(gLastAlign);
    refreshConnections();
    refreshSprite();
    validate();
    view_->fitAll();
}

void ImportPreviewDialog::rotate(int quarterTurns) {
    // The checkboxes stay by index: turning keeps the connections' order.
    rotatePart(base_, quarterTurns);
    realign();
    view_->fitAll();
}

void ImportPreviewDialog::realign() {
    part_ = alignPart(base_, align_, nudge_);
    refreshConnections();
    refreshSprite();
}

double ImportPreviewDialog::nudgeStep() const {
    return stepBox_ ? stepBox_->currentData().toDouble() : 0.25;
}

void ImportPreviewDialog::nudge(QPointF studs) {
    nudge_ += studs;
    realign();
}

void ImportPreviewDialog::setAlign(ImportAlign align) {
    align_ = align;
    gLastAlign = align;
    if (alignBox_) {
        const QSignalBlocker block(alignBox_);
        alignBox_->setCurrentIndex(alignBox_->findData(static_cast<int>(align)));
    }
    realign();
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
    {
        // The stud grid, so you see which studs line up.
        const double gx = part_.widthStuds > 0 ? part_.sprite.width() / double(part_.widthStuds) : 8.0;
        const double gy = part_.heightStuds > 0 ? part_.sprite.height() / double(part_.heightStuds) : 8.0;
        QPen grid(QColor(0, 120, 255, 90));
        grid.setCosmetic(true);
        for (int i = 0; i <= part_.widthStuds; ++i)
            scene_->addLine(i * gx, 0, i * gx, part_.sprite.height(), grid)->setData(0, QStringLiteral("grid"));
        for (int j = 0; j <= part_.heightStuds; ++j)
            scene_->addLine(0, j * gy, part_.sprite.width(), j * gy, grid)->setData(0, QStringLiteral("grid"));
    }
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

void ImportPreviewDialog::presetAlignment(ImportAlign align, QPointF nudgeStuds) {
    nudge_ = nudgeStuds;
    setAlign(align);
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
