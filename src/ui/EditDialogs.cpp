#include "EditDialogs.h"
#include <QUrl>
#include "help/HelpButton.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../edit/RulerCommands.h"
#include "../edit/TextCommands.h"
#include "../parts/PartsLibrary.h"

#include <QLabel>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>

namespace bld::ui {

namespace {

core::Brick* findBrickMut(core::Map& map, int layerIndex, const QString& guid) {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[layerIndex].get();
    if (!L || L->kind() != core::LayerKind::Brick) return nullptr;
    for (auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
        if (b.guid == guid) return &b;
    }
    return nullptr;
}

core::LayerRuler* rulerLayer(core::Map& map, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[layerIndex].get();
    return (L && L->kind() == core::LayerKind::Ruler) ? static_cast<core::LayerRuler*>(L) : nullptr;
}

core::LayerText* textLayer(core::Map& map, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[layerIndex].get();
    return (L && L->kind() == core::LayerKind::Text) ? static_cast<core::LayerText*>(L) : nullptr;
}

// Small helper: a push-button that opens a color picker, storing the picked
// color back to its caller-owned QColor. The button face is repainted with
// the current color so the user can see what they've chosen.
QPushButton* makeColorButton(QWidget* parent, QColor* target) {
    auto* btn = new QPushButton(parent);
    auto refresh = [btn, target]{
        QPixmap pm(24, 16); pm.fill(*target);
        btn->setIcon(QIcon(pm));
        btn->setText(target->name(QColor::HexArgb));
    };
    refresh();
    QObject::connect(btn, &QPushButton::clicked, btn, [btn, target, refresh]{
        const QColor c = QColorDialog::getColor(
            *target, btn, QObject::tr("Pick color"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        *target = c;
        refresh();
    });
    return btn;
}

int unitComboToIndex(int unit) {
    return (unit >= 0 && unit <= 5) ? unit : 0;
}

}  // namespace

bool editBrickDialog(QWidget* parent, core::Map& map, int layerIndex,
                     const QString& brickGuid, parts::PartsLibrary& lib,
                     QUndoStack& undoStack) {
    auto* brick = findBrickMut(map, layerIndex, brickGuid);
    if (!brick) return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Edit brick"));
    auto* form = new QFormLayout(&dlg);

    auto* partE = new QLineEdit(brick->partNumber, &dlg);
    partE->setToolTip(QObject::tr("Part key — e.g. 3001.1. Empty keeps current."));
    form->addRow(QObject::tr("Part:"), partE);

    auto* xSpin = new QDoubleSpinBox(&dlg);
    xSpin->setRange(-1e6, 1e6); xSpin->setDecimals(3);
    xSpin->setValue(brick->displayArea.x());
    auto* ySpin = new QDoubleSpinBox(&dlg);
    ySpin->setRange(-1e6, 1e6); ySpin->setDecimals(3);
    ySpin->setValue(brick->displayArea.y());
    form->addRow(QObject::tr("X (studs):"), xSpin);
    form->addRow(QObject::tr("Y (studs):"), ySpin);

    auto* rotSpin = new QDoubleSpinBox(&dlg);
    rotSpin->setRange(-360.0, 360.0); rotSpin->setDecimals(3); rotSpin->setSuffix(QStringLiteral("°"));
    rotSpin->setValue(brick->orientation);
    form->addRow(QObject::tr("Rotation:"), rotSpin);

    auto* altSpin = new QDoubleSpinBox(&dlg);
    altSpin->setRange(-1e4, 1e4); altSpin->setDecimals(2);
    altSpin->setValue(brick->altitude);
    form->addRow(QObject::tr("Altitude:"), altSpin);

    auto* connSpin = new QSpinBox(&dlg);
    auto meta = lib.metadata(brick->partNumber);
    const int nConn = meta ? meta->connections.size() : 0;
    connSpin->setRange(0, std::max(0, nConn - 1));
    connSpin->setValue(std::min(brick->activeConnectionPointIndex, std::max(0, nConn - 1)));
    connSpin->setEnabled(nConn > 0);
    form->addRow(QObject::tr("Active connection #:"), connSpin);
    if (meta && !meta->designer.isEmpty()) {
        // Credit for whoever built an imported model.
        const QString who = meta->designer.toHtmlEscaped();
        const QUrl url(meta->designerUrl);
        auto* by = new QLabel(url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
                                  ? QObject::tr("Designed by <a href=\"%1\">%2</a>").arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(), who)
                                  : QObject::tr("Designed by %1").arg(who),
                              &dlg);
        by->setObjectName(QStringLiteral("designer"));
        by->setTextFormat(Qt::RichText);
        by->setOpenExternalLinks(true);
        form->addRow(by);
    }
    if (meta && meta->importedFrom) {
        // Quietly: where an imported part came from.
        auto* from = new QLabel(QObject::tr("Imported from %1").arg(meta->importedFrom->file), &dlg);
        from->setObjectName(QStringLiteral("importedFrom"));
        from->setEnabled(false);
        if (!meta->importedFrom->date.isEmpty()) from->setToolTip(meta->importedFrom->date);
        form->addRow(from);
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;

    edit::EditBrickCommand::State before{
        brick->partNumber,
        brick->displayArea.topLeft(),
        brick->orientation,
        brick->altitude,
        brick->activeConnectionPointIndex,
    };
    edit::EditBrickCommand::State after{
        partE->text().isEmpty() ? brick->partNumber : partE->text(),
        QPointF(xSpin->value(), ySpin->value()),
        static_cast<float>(rotSpin->value()),
        static_cast<float>(altSpin->value()),
        connSpin->value(),
    };
    // No-op guard: dialog accepted but nothing changed.
    if (before.partNumber == after.partNumber &&
        before.topLeft == after.topLeft &&
        qFuzzyCompare(before.orientation, after.orientation) &&
        qFuzzyCompare(before.altitude,    after.altitude) &&
        before.activeConnectionPointIndex == after.activeConnectionPointIndex) {
        return false;
    }

    undoStack.push(new edit::EditBrickCommand(
        map, edit::BrickRef{ layerIndex, brickGuid }, before, after));
    return true;
}

bool editRulerDialog(QWidget* parent, core::Map& map, int layerIndex,
                     const QString& rulerGuid, QUndoStack& undoStack) {
    auto* L = rulerLayer(map, layerIndex);
    if (!L) return false;
    core::RulerItemBase* base = nullptr;
    for (auto& any : L->rulers) {
        auto& b = (any.kind == core::RulerKind::Linear)
                      ? static_cast<core::RulerItemBase&>(any.linear)
                      : static_cast<core::RulerItemBase&>(any.circular);
        if (b.guid == rulerGuid) { base = &b; break; }
    }
    if (!base) return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Edit ruler"));
    auto* form = new QFormLayout(&dlg);
    form->addRow(help::headingWithHelp(QObject::tr("Measure"), QStringLiteral("dialog.measure"), &dlg));

    QColor lineColor = base->color.color;
    auto* lineColorBtn = makeColorButton(&dlg, &lineColor);
    form->addRow(QObject::tr("Line color:"), lineColorBtn);

    auto* thickSpin = new QDoubleSpinBox(&dlg);
    thickSpin->setRange(0.1, 50.0); thickSpin->setDecimals(2);
    thickSpin->setValue(base->lineThickness);
    form->addRow(QObject::tr("Line thickness:"), thickSpin);

    auto* displayDist = new QCheckBox(QObject::tr("Display distance"), &dlg);
    displayDist->setChecked(base->displayDistance);
    form->addRow(displayDist);
    auto* displayUnit = new QCheckBox(QObject::tr("Append unit"), &dlg);
    displayUnit->setChecked(base->displayUnit);
    form->addRow(displayUnit);

    auto* unitCombo = new QComboBox(&dlg);
    for (const QString& s : { QStringLiteral("Studs"), QStringLiteral("LDU"),
                               QStringLiteral("Straight track (16 studs)"),
                               QStringLiteral("AFOL Module (96 studs)"),
                               QStringLiteral("Meters"), QStringLiteral("Feet") }) {
        unitCombo->addItem(s);
    }
    unitCombo->setCurrentIndex(unitComboToIndex(base->unit));
    form->addRow(QObject::tr("Unit:"), unitCombo);

    QColor guideColor = base->guidelineColor.color;
    auto* guideColorBtn = makeColorButton(&dlg, &guideColor);
    form->addRow(QObject::tr("Guideline color:"), guideColorBtn);
    auto* guideThickSpin = new QDoubleSpinBox(&dlg);
    guideThickSpin->setRange(0.0, 50.0); guideThickSpin->setDecimals(2);
    guideThickSpin->setValue(base->guidelineThickness);
    form->addRow(QObject::tr("Guideline thickness:"), guideThickSpin);

    auto* dashEdit = new QLineEdit(&dlg);
    QStringList dashStrs;
    for (float f : base->guidelineDashPattern) dashStrs << QString::number(f);
    dashEdit->setText(dashStrs.join(','));
    dashEdit->setToolTip(QObject::tr("Comma-separated dash lengths (e.g. 4,2,1,2)"));
    form->addRow(QObject::tr("Dash pattern:"), dashEdit);

    QColor measureColor = base->measureFontColor.color;
    auto* measureColorBtn = makeColorButton(&dlg, &measureColor);
    form->addRow(QObject::tr("Label color:"), measureColorBtn);
    auto* measureFont = new QFontComboBox(&dlg);
    measureFont->setCurrentFont(QFont(base->measureFont.familyName));
    form->addRow(QObject::tr("Label font:"), measureFont);
    auto* measureSize = new QDoubleSpinBox(&dlg);
    measureSize->setRange(1.0, 200.0); measureSize->setDecimals(2);
    measureSize->setValue(base->measureFont.sizePt);
    form->addRow(QObject::tr("Label size (pt):"), measureSize);

    // Attachment section. Linear rulers carry two endpoint attachments
    // (attachedBrick1Id / attachedBrick2Id), circular rulers a single
    // attachedBrickId. We display the current attachment brick guid(s) and
    // provide a Detach button per endpoint so the user can release them.
    // (Attaching via dialog requires picking a brick — we don't have a mini-
    // picker yet, so attach is done via the Map context menu.)
    {
        for (auto& any : L->rulers) {
            const QString& g = (any.kind == core::RulerKind::Linear) ? any.linear.guid : any.circular.guid;
            if (g != rulerGuid) continue;
            if (any.kind == core::RulerKind::Linear) {
                auto* btn1 = new QPushButton(any.linear.attachedBrick1Id.isEmpty()
                    ? QObject::tr("Not attached") : QObject::tr("Detach endpoint 1"), &dlg);
                btn1->setEnabled(!any.linear.attachedBrick1Id.isEmpty());
                QObject::connect(btn1, &QPushButton::clicked, &dlg, [&map, layerIndex, rulerGuid, &undoStack, &dlg]{
                    undoStack.push(new edit::AttachRulerCommand(map, layerIndex, rulerGuid, 0, QString()));
                    dlg.accept();  // close: renderer needs to rebuild
                });
                form->addRow(QObject::tr("Endpoint 1:"), btn1);
                auto* btn2 = new QPushButton(any.linear.attachedBrick2Id.isEmpty()
                    ? QObject::tr("Not attached") : QObject::tr("Detach endpoint 2"), &dlg);
                btn2->setEnabled(!any.linear.attachedBrick2Id.isEmpty());
                QObject::connect(btn2, &QPushButton::clicked, &dlg, [&map, layerIndex, rulerGuid, &undoStack, &dlg]{
                    undoStack.push(new edit::AttachRulerCommand(map, layerIndex, rulerGuid, 1, QString()));
                    dlg.accept();
                });
                form->addRow(QObject::tr("Endpoint 2:"), btn2);
            } else {
                auto* btn = new QPushButton(any.circular.attachedBrickId.isEmpty()
                    ? QObject::tr("Not attached") : QObject::tr("Detach centre"), &dlg);
                btn->setEnabled(!any.circular.attachedBrickId.isEmpty());
                QObject::connect(btn, &QPushButton::clicked, &dlg, [&map, layerIndex, rulerGuid, &undoStack, &dlg]{
                    undoStack.push(new edit::AttachRulerCommand(map, layerIndex, rulerGuid, 0, QString()));
                    dlg.accept();
                });
                form->addRow(QObject::tr("Attachment:"), btn);
            }
            break;
        }
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;

    edit::EditRulerItemCommand::BaseProps next;
    next.color = core::ColorSpec::fromArgb(lineColor);
    next.lineThickness = static_cast<float>(thickSpin->value());
    next.displayDistance = displayDist->isChecked();
    next.displayUnit = displayUnit->isChecked();
    next.guidelineColor = core::ColorSpec::fromArgb(guideColor);
    next.guidelineThickness = static_cast<float>(guideThickSpin->value());
    for (const QString& tok : dashEdit->text().split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const float f = tok.trimmed().toFloat(&ok);
        if (ok) next.guidelineDashPattern.push_back(f);
    }
    next.unit = unitCombo->currentIndex();
    next.measureFont.familyName = measureFont->currentFont().family();
    next.measureFont.sizePt = static_cast<float>(measureSize->value());
    next.measureFont.styleString = base->measureFont.styleString;  // preserve bold/italic
    next.measureFontColor = core::ColorSpec::fromArgb(measureColor);

    undoStack.push(new edit::EditRulerItemCommand(map, layerIndex, rulerGuid, next));
    return true;
}

bool editTextDialog(QWidget* parent, core::Map& map, int layerIndex,
                    const QString& cellGuid, QUndoStack& undoStack) {
    auto* L = textLayer(map, layerIndex);
    if (!L) return false;
    core::TextCell* cell = nullptr;
    for (auto& c : L->textCells) if (c.guid == cellGuid) { cell = &c; break; }
    if (!cell) return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Edit text"));
    auto* form = new QFormLayout(&dlg);

    auto* textE = new QPlainTextEdit(cell->text, &dlg);
    textE->setMinimumHeight(80);
    form->addRow(QObject::tr("Text:"), textE);

    auto* fontBox = new QFontComboBox(&dlg);
    fontBox->setCurrentFont(QFont(cell->font.familyName));
    form->addRow(QObject::tr("Font:"), fontBox);
    auto* sizeSpin = new QDoubleSpinBox(&dlg);
    sizeSpin->setRange(1.0, 500.0); sizeSpin->setDecimals(2);
    sizeSpin->setValue(cell->font.sizePt);
    form->addRow(QObject::tr("Size (pt):"), sizeSpin);
    auto* boldChk = new QCheckBox(QObject::tr("Bold"), &dlg);
    boldChk->setChecked(cell->font.styleString.contains(QStringLiteral("Bold")));
    form->addRow(boldChk);
    auto* italChk = new QCheckBox(QObject::tr("Italic"), &dlg);
    italChk->setChecked(cell->font.styleString.contains(QStringLiteral("Italic")));
    form->addRow(italChk);

    QColor color = cell->fontColor.color;
    auto* colorBtn = makeColorButton(&dlg, &color);
    form->addRow(QObject::tr("Color:"), colorBtn);

    auto* orientSpin = new QDoubleSpinBox(&dlg);
    orientSpin->setRange(-360.0, 360.0); orientSpin->setSuffix(QStringLiteral("°"));
    orientSpin->setValue(cell->orientation);
    form->addRow(QObject::tr("Rotation:"), orientSpin);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;

    QStringList styleParts;
    if (boldChk->isChecked())  styleParts << QStringLiteral("Bold");
    if (italChk->isChecked())  styleParts << QStringLiteral("Italic");
    if (styleParts.isEmpty())  styleParts << QStringLiteral("Regular");

    // Write directly (batched as a single macro on the undo stack so it's one
    // user-visible step even though it chains the existing text-only command
    // with an ad-hoc lambda edit of the other fields).
    undoStack.beginMacro(QObject::tr("Edit text"));
    undoStack.push(new edit::EditTextCellTextCommand(map, layerIndex, cellGuid, textE->toPlainText()));
    // Per-field updates are written directly and wrapped in a trivial restore
    // command so undo returns the prior state.
    class RestoreTextProps : public QUndoCommand {
    public:
        RestoreTextProps(core::Map& m, int li, QString g,
                         core::FontSpec b_font, core::ColorSpec b_color, float b_orient,
                         core::FontSpec a_font, core::ColorSpec a_color, float a_orient)
            : map_(m), li_(li), g_(std::move(g)),
              bf_(std::move(b_font)), bc_(std::move(b_color)), bo_(b_orient),
              af_(std::move(a_font)), ac_(std::move(a_color)), ao_(a_orient) {}
        void redo() override { apply(af_, ac_, ao_); }
        void undo() override { apply(bf_, bc_, bo_); }
    private:
        void apply(const core::FontSpec& f, const core::ColorSpec& c, float o) {
            if (li_ < 0 || li_ >= static_cast<int>(map_.layers().size())) return;
            auto* L = map_.layers()[li_].get();
            if (!L || L->kind() != core::LayerKind::Text) return;
            for (auto& cell : static_cast<core::LayerText&>(*L).textCells) {
                if (cell.guid == g_) {
                    cell.font = f; cell.fontColor = c; cell.orientation = o;
                    break;
                }
            }
        }
        core::Map& map_; int li_; QString g_;
        core::FontSpec bf_;
        core::ColorSpec bc_;
        float bo_;
        core::FontSpec af_;
        core::ColorSpec ac_;
        float ao_;
    };
    core::FontSpec newFont;
    newFont.familyName = fontBox->currentFont().family();
    newFont.sizePt = static_cast<float>(sizeSpin->value());
    newFont.styleString = styleParts.join(QStringLiteral(", "));
    undoStack.push(new RestoreTextProps(
        map, layerIndex, cellGuid,
        cell->font, cell->fontColor, cell->orientation,
        newFont, core::ColorSpec::fromArgb(color), static_cast<float>(orientSpin->value())));
    undoStack.endMacro();
    return true;
}

}
