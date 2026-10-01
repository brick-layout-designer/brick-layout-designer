#include "VenueDesignerDialog.h"

#include "VenueDesignerView.h"
#include "ui/help/HelpButton.h"
#include "ui/tours/Tours.h"

#include "saveload/VenueJson.h"

#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace bld::ui {

namespace ev = edit::venue;

namespace {

constexpr int kPlanMaxPx = 2400;
constexpr double kFt = 12 * ev::kStudsPerInch;

// A line edit for a length: shown in `unit`, applied on Enter or leaving it.
QLineEdit* lengthEdit(QWidget* parent, double studs, ev::LengthUnit unit, std::function<void(double)> apply) {
    auto* e = new QLineEdit(ev::formatLength(studs, unit), parent);
    e->setFont(QFont(QStringLiteral("monospace")));
    QObject::connect(e, &QLineEdit::editingFinished, e,
                     [e, unit, apply = std::move(apply), shown = e->text()] {
                         if (e->text() == shown) return;
                         const auto v = ev::parseLength(e->text(), unit);
                         if (!v || *v < 0) {
                             e->setStyleSheet(QStringLiteral("border: 1px solid #d33;"));
                             return;
                         }
                         apply(*v);
                     });
    return e;
}

QLineEdit* textEdit(QWidget* parent, const QString& value, std::function<void(const QString&)> apply) {
    auto* e = new QLineEdit(value, parent);
    QObject::connect(e, &QLineEdit::editingFinished, e, [e, value, apply = std::move(apply)] {
        if (e->text() != value) apply(e->text());
    });
    return e;
}

} // namespace

std::optional<FloorPlan> floorPlanOf(const core::Venue& v) {
    const QJsonObject p = v.extras.value(QLatin1String("floorPlan")).toObject();
    const QString url = p.value(QLatin1String("image")).toString();
    const int comma = url.indexOf(QLatin1Char(','));
    if (!url.startsWith(QLatin1String("data:image/")) || comma < 0) return std::nullopt;
    QImage img;
    if (!img.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1())) || img.isNull())
        return std::nullopt;
    FloorPlan out;
    out.image = img;
    out.topLeft = { p.value(QLatin1String("x")).toDouble(), p.value(QLatin1String("y")).toDouble() };
    const double spp = p.value(QLatin1String("studsPerPx")).toDouble(1.0);
    out.studsPerPx = std::isfinite(spp) && spp > 0 ? spp : 1.0;
    out.opacity = std::clamp(p.value(QLatin1String("opacity")).toDouble(0.4), 0.0, 1.0);
    return out;
}

core::Venue withFloorPlan(core::Venue v, const std::optional<FloorPlan>& plan) {
    if (!plan) {
        v.extras.remove(QStringLiteral("floorPlan"));
        return v;
    }
    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    plan->image.save(&buf, "JPEG", 85);
    v.extras.insert(QStringLiteral("floorPlan"),
                    QJsonObject{ { QStringLiteral("image"), QStringLiteral("data:image/jpeg;base64,")
                                                                + QString::fromLatin1(jpeg.toBase64()) },
                                 { QStringLiteral("x"), plan->topLeft.x() },
                                 { QStringLiteral("y"), plan->topLeft.y() },
                                 { QStringLiteral("studsPerPx"), plan->studsPerPx },
                                 { QStringLiteral("opacity"), plan->opacity } });
    return v;
}

FloorPlan calibratePlan(FloorPlan plan, QPointF a, QPointF b, double realStuds) {
    const double measured = ev::dist(a, b);
    if (measured < 0.001 || realStuds <= 0) return plan;
    const double k = realStuds / measured;
    plan.studsPerPx *= k;
    plan.topLeft = a - (a - plan.topLeft) * k;
    return plan;
}

VenueDesignerDialog::VenueDesignerDialog(core::Venue initial, const QString& subtitle,
                                         const QString& saveLabel,
                                         std::function<QString(const core::Venue&)> save, QWidget* parent)
    : QDialog(parent), state_(ev::initialState(initial)), saved_(std::move(initial)), save_(std::move(save)) {
    setWindowTitle(tr("Venue Designer"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    resize(1400, 880);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto* header = new QHBoxLayout();
    header->setContentsMargins(12, 8, 12, 8);
    auto* titles = new QVBoxLayout();
    title_ = new QLabel(this);
    QFont tf = title_->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    title_->setFont(tf);
    titles->addWidget(title_);
    auto* sub = new QLabel(subtitle, this);
    sub->setStyleSheet(QStringLiteral("color: palette(mid);"));
    titles->addWidget(sub);
    header->addLayout(titles, 1);
    header->addWidget(new QLabel(tr("Units"), this));
    unit_ = new QComboBox(this);
    unit_->addItems({ tr("Feet & inches"), tr("Metres"), tr("Studs") });
    connect(unit_, &QComboBox::currentIndexChanged, this,
            [this](int i) { dispatch(ev::act::Unit{ static_cast<ev::LengthUnit>(i) }); });
    header->addWidget(unit_);
    header->addWidget(new help::HelpButton(QStringLiteral("room.units"), this, unit_));
    snap_ = new QCheckBox(tr("Snap"), this);
    snap_->setChecked(true);
    snap_->setToolTip(tr("Snap to corners, walls, 45° and whole inches (hold Shift for any angle)"));
    connect(snap_, &QCheckBox::toggled, this, [this](bool on) { dispatch(ev::act::Snap{ on }); });
    header->addWidget(snap_);
    header->addWidget(new help::HelpButton(QStringLiteral("room.snap"), this, snap_));
    undo_ = new QPushButton(tr("Undo"), this);
    redo_ = new QPushButton(tr("Redo"), this);
    connect(undo_, &QPushButton::clicked, this, [this] { dispatch(ev::act::Undo{}); });
    connect(redo_, &QPushButton::clicked, this, [this] { dispatch(ev::act::Redo{}); });
    header->addWidget(undo_);
    header->addWidget(redo_);
    auto* planBtn = new QPushButton(tr("Floor plan..."), this);
    connect(planBtn, &QPushButton::clicked, this, &VenueDesignerDialog::loadPlan);
    header->addWidget(planBtn);
    header->addWidget(new help::HelpButton(QStringLiteral("room.floorPlan"), this, planBtn));
    saveBtn_ = new QPushButton(saveLabel, this);
    saveBtn_->setDefault(false);
    saveBtn_->setAutoDefault(false);
    tours::tag(saveBtn_, QStringLiteral("roomDesigner.save"));
    connect(saveBtn_, &QPushButton::clicked, this, [this] { saveNow(); });
    header->addWidget(saveBtn_);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setAutoDefault(false);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    header->addWidget(closeBtn);
    for (auto* b : { undo_, redo_, planBtn }) b->setAutoDefault(false);
    root->addLayout(header);

    auto* body = new QHBoxLayout();
    body->setSpacing(0);
    // Tools
    auto* toolBox = new QWidget(this);
    toolBox->setObjectName(QStringLiteral("RoomTools"));
    auto* toolCol = new QVBoxLayout(toolBox);
    toolCol->setContentsMargins(6, 6, 6, 6);
    toolCol->addWidget(new help::HelpButton(QStringLiteral("room.tools"), toolBox, toolBox), 0, Qt::AlignHCenter);
    auto* group = new QButtonGroup(this);
    for (const auto& t : ev::tools()) {
        auto* b = new QToolButton(this);
        b->setText(QStringLiteral("%1\n%2").arg(tr(t.label)).arg(QChar::fromLatin1(t.key).toUpper()));
        b->setToolTip(tr(t.hint));
        b->setCheckable(true);
        b->setMinimumSize(64, 44);
        b->setProperty("tool", static_cast<int>(t.tool));
        group->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, tool = t.tool] {
            planMoving_ = false;
            view_->setPlanMoving(false);
            dispatch(ev::act::SetTool{ tool });
        });
        toolCol->addWidget(b);
        toolButtons_ << b;
    }
    toolCol->addStretch(1);
    body->addWidget(toolBox);

    // Layers + view + status
    auto* center = new QVBoxLayout();
    auto* layers = new QHBoxLayout();
    layers->setContentsMargins(12, 4, 12, 4);
    auto* showLabel = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Show")), this);
    layers->addWidget(showLabel);
    layers->addWidget(new help::HelpButton(QStringLiteral("room.show"), this, showLabel));
    const std::pair<ev::Layer, QString> layerNames[] = {
        { ev::Layer::Plan, tr("Floor plan") }, { ev::Layer::Obstacles, tr("Obstacles") },
        { ev::Layer::Power, tr("Power") },     { ev::Layer::Dimensions, tr("Measurements") },
        { ev::Layer::Notes, tr("Notes") },     { ev::Layer::Estimates, tr("Estimates") },
    };
    for (const auto& [layer, name] : layerNames) {
        auto* c = new QCheckBox(name, this);
        c->setChecked(true);
        connect(c, &QCheckBox::toggled, this,
                [this, layer = layer](bool on) { dispatch(ev::act::Show{ layer, on }); });
        layers->addWidget(c);
    }
    layers->addStretch(1);
    estimates_ = new QLabel(this);
    estimates_->setStyleSheet(QStringLiteral("color: #b7791f;"));
    layers->addWidget(estimates_);
    center->addLayout(layers);
    view_ = new VenueDesignerView(this);
    center->addWidget(view_, 1);
    auto* status = new QHBoxLayout();
    status->setContentsMargins(12, 4, 12, 4);
    cursor_ = new QLabel(this);
    cursor_->setFont(QFont(QStringLiteral("monospace")));
    size_ = new QLabel(this);
    hint_ = new QLabel(this);
    status->addWidget(cursor_);
    status->addSpacing(24);
    status->addWidget(size_);
    status->addSpacing(24);
    status->addWidget(hint_, 1);
    auto* zoomOut = new QPushButton(QStringLiteral("−"), this);
    auto* zoomIn = new QPushButton(QStringLiteral("+"), this);
    auto* fitBtn = new QPushButton(tr("Fit"), this);
    for (auto* b : { zoomOut, zoomIn, fitBtn }) {
        b->setAutoDefault(false);
        status->addWidget(b);
    }
    connect(zoomOut, &QPushButton::clicked, this, [this] {
        view_->zoomBy(1 / 1.25);
        refresh();
    });
    connect(zoomIn, &QPushButton::clicked, this, [this] {
        view_->zoomBy(1.25);
        refresh();
    });
    connect(fitBtn, &QPushButton::clicked, this, [this] {
        view_->fit();
        refresh();
    });
    center->addLayout(status);
    body->addLayout(center, 1);

    inspector_ = new QWidget(this);
    inspector_->setFixedWidth(320);
    inspectorLayout_ = new QVBoxLayout(inspector_);
    body->addWidget(inspector_);
    root->addLayout(body, 1);

    connect(view_, &VenueDesignerView::pressed, this,
            [this](QPointF at, bool free) { dispatch(ev::act::Down{ at, view_->tolStuds(), free }); });
    connect(view_, &VenueDesignerView::moved, this,
            [this](QPointF at, bool free) { dispatch(ev::act::Move{ at, view_->tolStuds(), free }); });
    connect(view_, &VenueDesignerView::released, this, [this] { dispatch(ev::act::Up{}); });
    connect(view_, &VenueDesignerView::cursorMoved, this, [this](std::optional<QPointF> p) {
        cursor_->setText(p ? QStringLiteral("%1, %2").arg(ev::formatLength(p->x(), state_.unit),
                                                          ev::formatLength(p->y(), state_.unit))
                           : QStringLiteral("—"));
    });
    connect(view_, &VenueDesignerView::planDragged, this, [this](QPointF d) {
        auto plan = floorPlanOf(venue());
        if (!plan) return;
        plan->topLeft += d;
        dispatch(ev::act::Edit{ withFloorPlan(venue(), plan) });
    });
    refresh();
}

bool VenueDesignerDialog::dirty() const {
    return saveload::venueToJson(venue()) != saveload::venueToJson(saved_);
}

void VenueDesignerDialog::dispatch(const ev::Action& a) {
    state_ = ev::reduce(std::move(state_), a);
    // Pointer moves and typing don't change what the inspector shows.
    if (!std::holds_alternative<ev::act::Move>(a) && !std::holds_alternative<ev::act::Type>(a)) ++version_;
    refresh();
}

bool VenueDesignerDialog::saveNow() {
    const QString err = save_ ? save_(venue()) : QString();
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Save venue"), err);
        return false;
    }
    saved_ = venue();
    refresh();
    return true;
}

void VenueDesignerDialog::reject() {
    if (dirty()) {
        const auto btn = QMessageBox::question(
            this, tr("Venue Designer"), tr("Save your changes to the venue before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Save && !saveNow()) return;
    }
    QDialog::reject();
}

void VenueDesignerDialog::keyPressEvent(QKeyEvent* e) {
    const bool mod = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    if (mod && e->key() == Qt::Key_Z)
        return dispatch(e->modifiers() & Qt::ShiftModifier ? ev::Action(ev::act::Redo{})
                                                           : ev::Action(ev::act::Undo{}));
    if (mod && e->key() == Qt::Key_Y) return dispatch(ev::act::Redo{});
    if (mod && e->key() == Qt::Key_D) return dispatch(ev::act::Duplicate{});
    if (mod) return QDialog::keyPressEvent(e);
    if (e->key() == Qt::Key_Escape) return dispatch(ev::act::Escape{});
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) return dispatch(ev::act::Enter{});
    const bool drawing = !state_.draft.isEmpty() || state_.cut;
    const QString t = e->text();
    if (drawing) {
        if (e->key() == Qt::Key_Backspace)
            return dispatch(ev::act::Type{ state_.typed.chopped(state_.typed.isEmpty() ? 0 : 1) });
        static const QRegularExpression typing(QStringLiteral(R"(^[0-9.'"′″ /xX×a-zA-Z]$)"));
        static const QRegularExpression starts(QStringLiteral(R"(^[0-9.]$)"));
        if (typing.match(t).hasMatch() && (!state_.typed.isEmpty() || starts.match(t).hasMatch()))
            return dispatch(ev::act::Type{ state_.typed + t });
    }
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && state_.selection)
        return dispatch(ev::act::Delete{});
    for (const auto& info : ev::tools())
        if (t.size() == 1 && t.at(0).toLower() == QLatin1Char(info.key))
            return dispatch(ev::act::SetTool{ info.tool });
    QDialog::keyPressEvent(e);
}

void VenueDesignerDialog::loadPlan() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Floor plan image"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
    if (path.isEmpty()) return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Floor plan"),
                             tr("Could not read %1: %2").arg(path, reader.errorString()));
        return;
    }
    if (std::max(img.width(), img.height()) > kPlanMaxPx)
        img = img.scaled(kPlanMaxPx, kPlanMaxPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    FloorPlan plan;
    plan.image = img.convertToFormat(QImage::Format_RGB32);
    plan.topLeft = view_->mapToScene(40, 40) / 8.0;
    plan.studsPerPx = 150 * kFt / plan.image.width(); // 150 ft wide until calibrated
    dispatch(ev::act::Edit{ withFloorPlan(venue(), plan) });
    dispatch(ev::act::SetTool{ ev::Tool::Calibrate });
}

void VenueDesignerDialog::refresh() {
    const core::Venue& v = venue();
    title_->setText(v.name.isEmpty() ? tr("Venue") : v.name);
    if (dirty())
        title_->setText(title_->text()
                        + QStringLiteral("  <span style='color:#b7791f;font-weight:normal'>%1</span>")
                              .arg(tr("unsaved")));
    undo_->setEnabled(!state_.history.past.empty());
    redo_->setEnabled(!state_.history.future.empty());
    saveBtn_->setEnabled(dirty());
    for (auto* b : toolButtons_) b->setChecked(b->property("tool").toInt() == static_cast<int>(state_.tool));
    const int est = ev::estimateCount(v);
    estimates_->setText(est ? tr("%n estimate(s) left to measure", nullptr, est) : QString());
    if (const auto rs = ev::roomSize(v))
        size_->setText(tr("Room %1 × %2 · %3 sq ft")
                           .arg(ev::formatLength(rs->w, state_.unit), ev::formatLength(rs->h, state_.unit))
                           .arg(std::lround(rs->area / (kFt * kFt))));
    else size_->clear();
    QString hint;
    if (!state_.typed.isEmpty()) hint = tr("Typing %1 — Enter to apply").arg(state_.typed);
    else if (!state_.message.isEmpty()) hint = state_.message;
    else if (state_.tool == ev::Tool::Calibrate)
        hint = state_.calibration.size() < 2
                   ? tr("Click two points on the floor plan whose real distance you know")
                   : tr("Enter the real distance between the two points in the inspector");
    else if (planMoving_) hint = tr("Drag to move the floor plan");
    else
        for (const auto& t : ev::tools())
            if (t.tool == state_.tool) hint = tr(t.hint);
    hint_->setText(hint);

    // The plan image is decoded once per change of its data.
    const QString key = v.extras.value(QLatin1String("floorPlan"))
                            .toObject()
                            .value(QLatin1String("image"))
                            .toString()
                            .left(64)
                        + QString::number(v.extras.value(QLatin1String("floorPlan"))
                                              .toObject()
                                              .value(QLatin1String("image"))
                                              .toString()
                                              .size());
    if (key != planCacheKey_) {
        planCache_ = floorPlanOf(v);
        planCacheKey_ = key;
    }
    std::optional<FloorPlan> plan = planCache_;
    if (plan) {
        const QJsonObject p = v.extras.value(QLatin1String("floorPlan")).toObject();
        plan->topLeft = { p.value(QLatin1String("x")).toDouble(), p.value(QLatin1String("y")).toDouble() };
        plan->studsPerPx = p.value(QLatin1String("studsPerPx")).toDouble(1);
        plan->opacity = p.value(QLatin1String("opacity")).toDouble(0.4);
    }
    view_->setState(
        state_, plan ? plan->image : QImage(),
        plan ? QRectF(plan->topLeft, QSizeF(plan->image.width(), plan->image.height()) * plan->studsPerPx)
             : QRectF(),
        plan ? plan->opacity : 0);
    if (inspected_ != state_.selection || (state_.drag == std::nullopt && inspectedVersion_ != version_))
        rebuildInspector();
}

void VenueDesignerDialog::rebuildInspector() {
    inspected_ = state_.selection;
    inspectedVersion_ = version_;
    while (auto* item = inspectorLayout_->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide(); // gone now, deleted once the event loop runs
            w->deleteLater();
        }
        delete item;
    }
    auto* w = new QWidget(inspector_);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    inspectorLayout_->addWidget(w, 1);
    const core::Venue& v = venue();
    const auto unit = state_.unit;
    const auto set = [this](core::Venue next) { dispatch(ev::act::Edit{ std::move(next) }); };
    auto* head = new QLabel(w);
    auto* form = new QFormLayout();
    col->addWidget(head);
    col->addLayout(form);
    const auto estimated = [&](bool on, std::function<void(bool)> apply) {
        auto* c = new QCheckBox(tr("Estimated, not measured yet"), w);
        c->setChecked(on);
        connect(c, &QCheckBox::toggled, this, std::move(apply));
        col->addWidget(help::withHelp(c, QStringLiteral("room.estimates"), w));
    };

    const auto sel = state_.selection;
    if (!sel) {
        head->setText(QStringLiteral("<b>%1</b>").arg(tr("Venue")));
        form->addRow(tr("Name"), textEdit(w, v.name, [=](const QString& t) {
                         if (t.trimmed().isEmpty()) return;
                         core::Venue n = venue();
                         n.name = t.trimmed();
                         set(n);
                     }));
        form->addRow(tr("Minimum walkway"), help::withHelp(lengthEdit(w, v.minWalkwayStuds, unit, [=](double s) {
                                                  core::Venue n = venue();
                                                  n.minWalkwayStuds = s;
                                                  set(n);
                                              }),
                                              QStringLiteral("room.walkway"), w));
        const auto rs = ev::roomSize(v);
        form->addRow(tr("Size"), new QLabel(rs ? QStringLiteral("%1 × %2").arg(ev::formatLength(rs->w, unit),
                                                                               ev::formatLength(rs->h, unit))
                                               : QStringLiteral("—"),
                                            w));
        form->addRow(tr("Walls, doors, openings"), new QLabel(QString::number(v.edges.size()), w));
        form->addRow(tr("Obstacles"), new QLabel(QString::number(v.obstacles.size()), w));
        form->addRow(tr("Power points"), new QLabel(QString::number(v.power.size()), w));
        form->addRow(tr("Still estimated"), new QLabel(QString::number(ev::estimateCount(v)), w));
    } else if (sel->kind == ev::PartKind::Edge && sel->index < v.edges.size()) {
        head->setText(QStringLiteral("<b>%1</b>").arg(tr("Wall")));
        const auto e = v.edges[sel->index];
        const int i = sel->index;
        form->addRow(tr("Label"), textEdit(w, e.label, [=](const QString& t) {
                         core::Venue n = venue();
                         n.edges[i].label = t;
                         set(n);
                     }));
        auto* kind = new QComboBox(w);
        kind->addItems({ tr("Wall"), tr("Door"), tr("Opening") });
        kind->setCurrentIndex(static_cast<int>(e.kind));
        connect(kind, &QComboBox::currentIndexChanged, this, [=](int k) {
            core::Venue n = venue();
            n.edges[i].kind = static_cast<core::EdgeKind>(k);
            n.edges[i].doorWidthStuds = k == 1 ? ev::polylineLength(n.edges[i].polyline) : 0;
            set(n);
        });
        form->addRow(tr("Kind"), kind);
        form->addRow(tr("Length"), lengthEdit(w, ev::polylineLength(e.polyline), unit, [=](double want) {
                         core::Venue n = venue();
                         auto& poly = n.edges[i].polyline;
                         const QPointF a = poly[poly.size() - 2], b = poly.last();
                         const double seg = ev::dist(a, b), k = (seg + want - ev::polylineLength(poly)) / seg;
                         if (seg < 0.001 || k <= 0) return;
                         poly.last() = a + (b - a) * k;
                         if (n.edges[i].kind == core::EdgeKind::Door) n.edges[i].doorWidthStuds = want;
                         set(n);
                     }));
        estimated(e.estimated, [=](bool on) {
            core::Venue n = venue();
            n.edges[i].estimated = on;
            set(n);
        });
    } else if (sel->kind == ev::PartKind::Obstacle && sel->index < v.obstacles.size()) {
        head->setText(QStringLiteral("<b>%1</b>").arg(tr("Obstacle")));
        const auto o = v.obstacles[sel->index];
        const int i = sel->index;
        const QRectF b = ev::bounds(o.polygon);
        form->addRow(tr("Label"), textEdit(w, o.label, [=](const QString& t) {
                         core::Venue n = venue();
                         n.obstacles[i].label = t;
                         set(n);
                     }));
        auto* kind = new QComboBox(w);
        const std::pair<core::ObstacleKind, QString> kinds[] = {
            { core::ObstacleKind::Column, tr("Column") },     { core::ObstacleKind::Stairs, tr("Stairs") },
            { core::ObstacleKind::Elevator, tr("Elevator") }, { core::ObstacleKind::Counter, tr("Counter") },
            { core::ObstacleKind::Railing, tr("Railing") },   { core::ObstacleKind::Other, tr("Other") }
        };
        for (const auto& [k, name] : kinds) {
            kind->addItem(name, static_cast<int>(k));
            if (k == o.kind) kind->setCurrentIndex(kind->count() - 1);
        }
        connect(kind, &QComboBox::currentIndexChanged, this, [=](int) {
            core::Venue n = venue();
            n.obstacles[i].kind = static_cast<core::ObstacleKind>(kind->currentData().toInt());
            if (n.obstacles[i].kind == core::ObstacleKind::Stairs && !n.obstacles[i].upDegrees)
                n.obstacles[i].upDegrees = 270.0;
            if (n.obstacles[i].kind != core::ObstacleKind::Stairs) n.obstacles[i].upDegrees.reset();
            set(n);
        });
        form->addRow(tr("Kind"), kind);
        form->addRow(tr("Width"), lengthEdit(w, b.width(), unit, [=](double x) {
                         set(ev::resizeObstacle(venue(), i, x, b.height()));
                     }));
        form->addRow(tr("Depth"), lengthEdit(w, b.height(), unit, [=](double y) {
                         set(ev::resizeObstacle(venue(), i, b.width(), y));
                     }));
        form->addRow(
            tr("From the left (x)"), lengthEdit(w, b.left(), unit, [=](double x) {
                set(ev::movePart(venue(), ev::Selection{ ev::PartKind::Obstacle, i }, { x - b.left(), 0 }));
            }));
        form->addRow(
            tr("From the top (y)"), lengthEdit(w, b.top(), unit, [=](double y) {
                set(ev::movePart(venue(), ev::Selection{ ev::PartKind::Obstacle, i }, { 0, y - b.top() }));
            }));
        if (o.kind == core::ObstacleKind::Stairs) {
            auto* ways = new QHBoxLayout();
            const std::pair<double, QString> dirs[] = {
                { 270, tr("North") }, { 0, tr("East") }, { 90, tr("South") }, { 180, tr("West") }
            };
            for (const auto& [deg, name] : dirs) {
                auto* btn = new QPushButton(name, w);
                btn->setCheckable(true);
                btn->setAutoDefault(false);
                btn->setChecked(std::abs(o.upDegrees.value_or(-1000.0) - deg) < 0.5);
                connect(btn, &QPushButton::clicked, this, [=, deg = deg] {
                    core::Venue n = venue();
                    n.obstacles[i].upDegrees = deg;
                    set(n);
                });
                ways->addWidget(btn);
            }
            form->addRow(tr("Way up"), ways);
        }
    } else if (sel->kind == ev::PartKind::Power && sel->index < v.power.size()) {
        head->setText(QStringLiteral("<b>%1</b>").arg(tr("Power point")));
        const auto p = v.power[sel->index];
        const int i = sel->index;
        form->addRow(tr("Label"), textEdit(w, p.label, [=](const QString& t) {
                         core::Venue n = venue();
                         n.power[i].label = t;
                         set(n);
                     }));
        auto* where = new QComboBox(w);
        where->addItems({ tr("Wall outlet"), tr("Floor outlet") });
        where->setCurrentIndex(p.floor ? 1 : 0);
        connect(where, &QComboBox::currentIndexChanged, this, [=](int k) {
            core::Venue n = venue();
            n.power[i].floor = k == 1;
            set(n);
        });
        form->addRow(tr("Where"), where);
        const auto number = [&](double value, const std::function<void(core::VenuePower&, double)>& put) {
            return textEdit(w, value > 0 ? QString::number(value) : QString(), [=](const QString& t) {
                core::Venue n = venue();
                put(n.power[i], std::max(0.0, t.toDouble()));
                set(n);
            });
        };
        form->addRow(tr("Amps"), number(p.amps, [](core::VenuePower& x, double a) { x.amps = a; }));
        form->addRow(tr("Volts"), number(p.volts, [](core::VenuePower& x, double a) { x.volts = a; }));
    } else if (sel->kind == ev::PartKind::Note && sel->index < v.notes.size()) {
        head->setText(QStringLiteral("<b>%1</b>").arg(tr("Note")));
        const auto nt = v.notes[sel->index];
        const int i = sel->index;
        auto* text = textEdit(w, nt.text, [=](const QString& t) {
            if (t.trimmed().isEmpty()) return;
            core::Venue n = venue();
            n.notes[i].text = t.trimmed();
            set(n);
        });
        form->addRow(tr("Text"), text);
        if (nt.text == QLatin1String("Note")) {
            text->setFocus();
            text->selectAll();
        }
        estimated(nt.estimated, [=](bool on) {
            core::Venue n = venue();
            n.notes[i].estimated = on;
            set(n);
        });
    } else if (sel->kind == ev::PartKind::Dimension && sel->index < v.dimensions.size()) {
        head->setText(QStringLiteral("<b>%1</b>").arg(tr("Measurement")));
        const auto d = v.dimensions[sel->index];
        const int i = sel->index;
        form->addRow(tr("Label"), textEdit(w, d.label, [=](const QString& t) {
                         core::Venue n = venue();
                         n.dimensions[i].label = t;
                         set(n);
                     }));
        const QString measured = ev::formatLength(ev::dist(d.from, d.to), unit);
        auto* use = new QPushButton(tr("Use %1 as the label").arg(measured), w);
        use->setAutoDefault(false);
        use->setEnabled(d.label != measured);
        connect(use, &QPushButton::clicked, this, [=] {
            core::Venue n = venue();
            n.dimensions[i].label = measured;
            set(n);
        });
        form->addRow(tr("Measures"), use);
        estimated(d.estimated, [=](bool on) {
            core::Venue n = venue();
            n.dimensions[i].estimated = on;
            set(n);
        });
    }

    // Floor plan
    if (const auto plan = floorPlanOf(v)) {
        auto* box = new QVBoxLayout();
        box->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Floor plan")), w));
        auto* opacity = new QSlider(Qt::Horizontal, w);
        opacity->setRange(10, 100);
        opacity->setValue(static_cast<int>(plan->opacity * 100));
        connect(opacity, &QSlider::sliderReleased, this, [=] {
            auto p = floorPlanOf(venue());
            if (!p) return;
            p->opacity = opacity->value() / 100.0;
            set(withFloorPlan(venue(), p));
        });
        box->addWidget(opacity);
        if (state_.calibration.size() == 2) {
            auto* real = new QLineEdit(w);
            real->setPlaceholderText(tr("Real distance, e.g. 40' 4\""));
            box->addWidget(new QLabel(tr("Real distance between the two points"), w));
            box->addWidget(real);
            auto* apply = new QPushButton(tr("Scale the floor plan"), w);
            apply->setAutoDefault(false);
            const auto calibrate = [=] {
                const auto v2 = ev::parseLength(real->text(), state_.unit);
                if (!v2 || *v2 <= 0) {
                    real->setStyleSheet(QStringLiteral("border: 1px solid #d33;"));
                    return;
                }
                auto p = floorPlanOf(venue());
                if (!p) return;
                const QPointF a = state_.calibration[0], b = state_.calibration[1];
                set(withFloorPlan(venue(), calibratePlan(*p, a, b, *v2)));
                dispatch(ev::act::SetTool{ ev::Tool::Select });
            };
            connect(apply, &QPushButton::clicked, this, calibrate);
            connect(real, &QLineEdit::returnPressed, this, calibrate);
            box->addWidget(apply);
            real->setFocus();
        } else {
            box->addWidget(new QLabel(
                tr("1 image pixel = %1. Calibrate by clicking two points you know the distance between.")
                    .arg(ev::formatLength(plan->studsPerPx, state_.unit)),
                w));
        }
        auto* row = new QHBoxLayout();
        auto* cal = new QPushButton(tr("Calibrate"), w);
        auto* move = new QPushButton(tr("Move"), w);
        auto* remove = new QPushButton(tr("Remove"), w);
        move->setCheckable(true);
        move->setChecked(planMoving_);
        for (auto* b : { cal, move, remove }) {
            b->setAutoDefault(false);
            row->addWidget(b);
            if (b == cal) row->addWidget(new help::HelpButton(QStringLiteral("room.calibrate"), w, cal));
        }
        connect(cal, &QPushButton::clicked, this, [this] {
            planMoving_ = false;
            view_->setPlanMoving(false);
            dispatch(ev::act::SetTool{ ev::Tool::Calibrate });
        });
        connect(move, &QPushButton::toggled, this, [this](bool on) {
            planMoving_ = on;
            view_->setPlanMoving(on);
            if (on) dispatch(ev::act::SetTool{ ev::Tool::Select });
        });
        connect(remove, &QPushButton::clicked, this, [=] { set(withFloorPlan(venue(), std::nullopt)); });
        box->addLayout(row);
        col->addLayout(box);
    }
    col->addStretch(1);
    if (sel) {
        auto* row = new QHBoxLayout();
        auto* dup = new QPushButton(tr("Duplicate"), w);
        auto* del = new QPushButton(tr("Delete"), w);
        for (auto* b : { dup, del }) {
            b->setAutoDefault(false);
            row->addWidget(b);
        }
        connect(dup, &QPushButton::clicked, this, [this] { dispatch(ev::act::Duplicate{}); });
        connect(del, &QPushButton::clicked, this, [this] { dispatch(ev::act::Delete{}); });
        col->addLayout(row);
    }
}

} // namespace bld::ui
