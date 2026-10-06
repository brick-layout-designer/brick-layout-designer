#include "VenueDimensionsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cmath>

namespace bld::ui {
namespace {
// Compass presets for the Angle column. Users can still type any custom
// numeric value (the combobox is editable) — these just make the common
// cases one click instead of remembering the convention.
struct AnglePreset { const char* label; double angleDeg; };
static const AnglePreset kAnglePresets[] = {
    { "→ East (0°)",      0.0   },
    { "↘ SE (45°)",       45.0  },
    { "↓ South (90°)",    90.0  },
    { "↙ SW (135°)",      135.0 },
    { "← West (180°)",    180.0 },
    { "↖ NW (225°)",      225.0 },
    { "↑ North (270°)",   270.0 },
    { "↗ NE (315°)",      315.0 },
};

QComboBox* makeAngleCombo(double initialDeg) {
    auto* c = new QComboBox();
    c->setEditable(true);  // lets the user type a custom angle
    for (const auto& p : kAnglePresets) {
        c->addItem(QObject::tr(p.label), p.angleDeg);
    }
    // Match the preset whose angle equals the initial value, or show the
    // numeric value directly so custom entries round-trip cleanly.
    int matchIdx = -1;
    for (int i = 0; i < c->count(); ++i) {
        if (std::abs(c->itemData(i).toDouble() - initialDeg) < 0.001) { matchIdx = i; break; }
    }
    if (matchIdx >= 0) c->setCurrentIndex(matchIdx);
    else               c->setCurrentText(QString::number(initialDeg, 'f', 2));
    return c;
}

double readAngleFromCombo(QComboBox* c) {
    if (!c) return 0.0;
    // If the displayed text matches a preset label exactly, trust its
    // stored data; otherwise parse the numeric text for custom entries.
    const int idx = c->findText(c->currentText());
    if (idx >= 0) return c->itemData(idx).toDouble();
    const QString raw = c->currentText().trimmed();
    // Preset labels contain "(N°)"; fall back to scanning for the
    // numeric portion so the user can type either "45" or "↘ SE (45°)".
    for (int i = 0; i < c->count(); ++i) {
        if (c->itemText(i) == raw) return c->itemData(i).toDouble();
    }
    bool ok = false;
    const double v = raw.toDouble(&ok);
    return ok ? v : 0.0;
}
}
}

namespace bld::ui {

VenueDimensionsDialog::VenueDimensionsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Draw venue outline by dimensions"));
    resize(600, 560);

    // Real-world venues are quoted in either feet OR inches depending on
    // the drawing. Data model stays in studs; conversion happens in the
    // OK handler based on the selected unit.
    static constexpr double kStudsPerFoot = 38.09814081;
    static constexpr double kStudsPerInch = kStudsPerFoot / 12.0;

    auto* vbox = new QVBoxLayout(this);

    auto* unitCombo = new QComboBox(this);
    unitCombo->addItem(tr("Feet (ft)"),   QStringLiteral("ft"));
    unitCombo->addItem(tr("Inches (in)"), QStringLiteral("in"));

    auto* form = new QFormLayout();
    form->addRow(tr("Unit:"), unitCombo);
    auto* originX = new QDoubleSpinBox(this);
    originX->setRange(-1e5, 1e5); originX->setDecimals(2);
    auto* originY = new QDoubleSpinBox(this);
    originY->setRange(-1e5, 1e5); originY->setDecimals(2);
    form->addRow(tr("Start X:"), originX);
    form->addRow(tr("Start Y:"), originY);
    vbox->addLayout(form);

    auto applyUnitSuffix = [originX, originY](const QString& unit){
        const QString s = QStringLiteral(" %1").arg(unit);
        originX->setSuffix(s);
        originY->setSuffix(s);
    };
    applyUnitSuffix(QStringLiteral("ft"));
    QObject::connect(unitCombo, &QComboBox::currentIndexChanged, this,
                     [unitCombo, applyUnitSuffix](int){
        applyUnitSuffix(unitCombo->currentData().toString());
    });

    vbox->addWidget(new QLabel(tr(
        "<b>Segments</b> — each row adds a new vertex at the given distance "
        "and angle from the previous point. Lengths are in <b>feet</b>. "
        "Angle: 0° east, 90° south, 180° west, 270° north. The polygon is "
        "closed automatically.")));

    auto* table = new QTableWidget(0, 4, this);
    const auto setHeader = [table](const QString& unit){
        table->setHorizontalHeaderLabels({ QObject::tr("Length (%1)").arg(unit),
                                            QObject::tr("Angle (°)"),
                                            QObject::tr("Kind"),
                                            QObject::tr("Label") });
    };
    setHeader(QStringLiteral("ft"));
    QObject::connect(unitCombo, &QComboBox::currentIndexChanged, this,
                     [unitCombo, setHeader](int){
        setHeader(unitCombo->currentData().toString());
    });
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(true);
    vbox->addWidget(table, 1);

    auto makeKindCombo = [](core::EdgeKind initial){
        auto* c = new QComboBox();
        c->addItem(QObject::tr("Wall"), int(core::EdgeKind::Wall));
        c->addItem(QObject::tr("Door"), int(core::EdgeKind::Door));
        c->addItem(QObject::tr("Open"), int(core::EdgeKind::Open));
        c->setCurrentIndex(static_cast<int>(initial));
        return c;
    };

    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("Add segment"), this);
    auto* remBtn = new QPushButton(tr("Remove last"), this);
    auto* preset = new QPushButton(tr("Rectangle preset…"), this);
    preset->setToolTip(tr("Quickly fill four segments for a width × depth rectangle."));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addStretch();
    btnRow->addWidget(preset);
    vbox->addLayout(btnRow);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    vbox->addWidget(bb);

    // Convenient default: one row ready for editing. Default length 10 ft
    // is a reasonable starter for typical venue walls.
    auto addRow = [table, makeKindCombo](double lengthFt = 10.0, double angle = 0.0,
                                          core::EdgeKind kind = core::EdgeKind::Wall,
                                          const QString& label = QString()) {
        const int r = table->rowCount();
        table->insertRow(r);
        auto* lenItem = new QTableWidgetItem(QString::number(lengthFt, 'f', 2));
        table->setItem(r, 0, lenItem);
        // Angle column: editable combobox with compass presets.
        table->setCellWidget(r, 1, makeAngleCombo(angle));
        // Kind column: Wall / Door / Open dropdown — set per-segment at
        // draw time so the user doesn't have to reopen Edit Venue
        // Properties to reclassify.
        table->setCellWidget(r, 2, makeKindCombo(kind));
        // Label column: free-form text (e.g. "Main entrance").
        table->setCellWidget(r, 3, new QLineEdit(label));
    };
    addRow();

    connect(addBtn, &QPushButton::clicked, this, [addRow]{ addRow(); });
    connect(remBtn, &QPushButton::clicked, this, [table]{
        if (table->rowCount() > 0) table->removeRow(table->rowCount() - 1);
    });
    connect(preset, &QPushButton::clicked, this, [this, table, makeKindCombo]{
        // Ask for width + depth (in feet) and replace the segment list
        // with a 4-row rectangle going east, south, west, north.
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Rectangle preset"));
        auto* f = new QFormLayout(&dlg);
        auto* w = new QDoubleSpinBox(&dlg);  w->setRange(0, 10000); w->setValue(30);
        auto* d = new QDoubleSpinBox(&dlg);  d->setRange(0, 10000); d->setValue(20);
        w->setSuffix(tr(" ft")); d->setSuffix(tr(" ft"));
        w->setDecimals(2);      d->setDecimals(2);
        f->addRow(tr("Width (east-west):"),  w);
        f->addRow(tr("Depth (north-south):"), d);
        auto* db = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        f->addRow(db);
        connect(db, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(db, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        table->setRowCount(0);
        const double widthFt = w->value();
        const double depthFt = d->value();
        auto append = [table, makeKindCombo](double lenFt, double ang) {
            const int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(QString::number(lenFt, 'f', 2)));
            table->setCellWidget(r, 1, makeAngleCombo(ang));
            table->setCellWidget(r, 2, makeKindCombo(core::EdgeKind::Wall));
            table->setCellWidget(r, 3, new QLineEdit());
        };
        append(widthFt, 0.0);
        append(depthFt, 90.0);
        append(widthFt, 180.0);
        append(depthFt, 270.0);
    });

    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this,
            [this, table, originX, originY, unitCombo]{
        const QString unit = unitCombo->currentData().toString();
        const double studsPerUnit = (unit == QStringLiteral("in")) ? kStudsPerInch
                                                                    : kStudsPerFoot;
        QVector<QPointF>     pts;
        QVector<SegmentMeta> metas;
        QPointF cur(originX->value() * studsPerUnit,
                    originY->value() * studsPerUnit);
        pts.push_back(cur);
        for (int r = 0; r < table->rowCount(); ++r) {
            auto* lenItem  = table->item(r, 0);
            auto* angCombo = qobject_cast<QComboBox*>(table->cellWidget(r, 1));
            auto* kindCombo = qobject_cast<QComboBox*>(table->cellWidget(r, 2));
            auto* labelEdit = qobject_cast<QLineEdit*>(table->cellWidget(r, 3));
            if (!lenItem || !angCombo) continue;
            const double lengthUnit = lenItem->text().toDouble();
            const double angDeg   = readAngleFromCombo(angCombo);
            if (lengthUnit <= 0.0) continue;
            const double lengthStuds = lengthUnit * studsPerUnit;
            const double rad = angDeg * M_PI / 180.0;
            cur += QPointF(std::cos(rad) * lengthStuds, std::sin(rad) * lengthStuds);
            pts.push_back(cur);

            SegmentMeta meta;
            meta.kind = kindCombo
                ? static_cast<core::EdgeKind>(kindCombo->currentData().toInt())
                : core::EdgeKind::Wall;
            meta.label = labelEdit ? labelEdit->text() : QString();
            metas.push_back(meta);
        }
        if (pts.size() < 3) {
            QMessageBox::information(this, tr("Venue outline"),
                tr("Need at least three non-zero segments to build a polygon."));
            return;
        }
        // Drop the closing vertex if it duplicates the origin (common when
        // the user manually entered the full loop). metas is indexed by
        // segment (not vertex) so no adjustment needed there.
        const QPointF delta = pts.last() - pts.first();
        if (std::hypot(delta.x(), delta.y()) < 0.5) pts.removeLast();
        polygon_ = std::move(pts);
        segments_ = std::move(metas);
        accept();
    });
}

}
