#include "VenueDialog.h"
#include "ConfirmDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <memory>

namespace bld::ui {

namespace {
const char* kindLabels[] = { "Wall", "Door", "Open" };
}

VenueDialog::VenueDialog(const std::optional<core::Venue>& current, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Venue"));
    resize(620, 520);

    // Heap-allocated working copy. Earlier this was a stack local
    // inside the ctor — every lambda captured `&venue` by reference
    // and deref'd it AFTER the ctor returned (exec() runs the event
    // loop and only then fires the OK handler), producing a segfault
    // on save. Shared-pointer capture keeps the storage alive until
    // every lambda is destroyed with the dialog.
    auto venue = std::make_shared<core::Venue>(current.value_or(core::Venue{}));

    auto* vbox = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    auto* nameE = new QLineEdit(venue->name, this);
    form->addRow(tr("Name:"), nameE);
    auto* enabledChk = new QCheckBox(tr("Render this venue"), this);
    enabledChk->setChecked(venue->enabled);
    form->addRow(enabledChk);
    static constexpr double kStudsPerFoot = 38.09814081;
    auto* walkwaySpin = new QDoubleSpinBox(this);
    walkwaySpin->setRange(0.0, 500.0);
    walkwaySpin->setDecimals(2);
    walkwaySpin->setSuffix(tr(" ft"));
    walkwaySpin->setValue(venue->minWalkwayStuds / kStudsPerFoot);
    form->addRow(tr("Min walkway:"), walkwaySpin);
    vbox->addLayout(form);

    vbox->addWidget(new QLabel(tr("<b>Edges</b> (from the drawn outline — classify each segment)")));
    auto* edgeTable = new QTableWidget(this);
    edgeTable->setColumnCount(4);
    edgeTable->setHorizontalHeaderLabels({ tr("#"), tr("Kind"), tr("Door width (ft)"), tr("Label") });
    edgeTable->horizontalHeader()->setStretchLastSection(true);
    edgeTable->verticalHeader()->setVisible(false);
    vbox->addWidget(edgeTable, 1);

    auto rebuildEdgeTable = [edgeTable, venue]{
        edgeTable->setRowCount(venue->edges.size());
        for (int i = 0; i < venue->edges.size(); ++i) {
            const auto& e = venue->edges[i];
            auto* idxItem = new QTableWidgetItem(QString::number(i + 1));
            idxItem->setFlags(idxItem->flags() & ~Qt::ItemIsEditable);
            edgeTable->setItem(i, 0, idxItem);

            auto* kindCombo = new QComboBox();
            for (const char* lbl : kindLabels) kindCombo->addItem(QObject::tr(lbl));
            kindCombo->setCurrentIndex(static_cast<int>(e.kind));
            edgeTable->setCellWidget(i, 1, kindCombo);

            auto* widthSpin = new QDoubleSpinBox();
            widthSpin->setRange(0.0, 500.0); widthSpin->setDecimals(2);
            widthSpin->setValue(e.doorWidthStuds / kStudsPerFoot);
            widthSpin->setEnabled(e.kind == core::EdgeKind::Door);
            edgeTable->setCellWidget(i, 2, widthSpin);
            QObject::connect(kindCombo, &QComboBox::currentIndexChanged, widthSpin,
                             [widthSpin](int k){ widthSpin->setEnabled(k == int(core::EdgeKind::Door)); });

            auto* labelEdit = new QLineEdit(e.label);
            edgeTable->setCellWidget(i, 3, labelEdit);
        }
        edgeTable->resizeColumnsToContents();
    };
    rebuildEdgeTable();

    auto* obstBox = new QLabel(
        tr("<b>Obstacles</b>: %1 polygon(s) drawn.").arg(venue->obstacles.size()), this);
    vbox->addWidget(obstBox);

    auto* btnRow = new QHBoxLayout();
    auto* clearBtn = new QPushButton(tr("Clear Venue"), this);
    clearBtn->setToolTip(tr("Remove the venue entirely."));
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    vbox->addLayout(btnRow);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    vbox->addWidget(bb);

    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this,
        [this, venue, nameE, enabledChk, walkwaySpin, edgeTable]{
        // Pull back edge kind / width / label from the table widgets.
        for (int i = 0; i < venue->edges.size(); ++i) {
            auto* kindCombo = qobject_cast<QComboBox*>(edgeTable->cellWidget(i, 1));
            auto* widthSpin = qobject_cast<QDoubleSpinBox*>(edgeTable->cellWidget(i, 2));
            auto* labelEdit = qobject_cast<QLineEdit*>(edgeTable->cellWidget(i, 3));
            if (kindCombo) venue->edges[i].kind = static_cast<core::EdgeKind>(kindCombo->currentIndex());
            if (widthSpin) venue->edges[i].doorWidthStuds = widthSpin->value() * kStudsPerFoot;
            if (labelEdit) venue->edges[i].label = labelEdit->text();
        }
        venue->name = nameE->text();
        venue->enabled = enabledChk->isChecked();
        venue->minWalkwayStuds = walkwaySpin->value() * kStudsPerFoot;
        result_ = *venue;
        cleared_ = false;
        accept();
    });

    connect(clearBtn, &QPushButton::clicked, this, [this]{
        ConfirmOptions clearVenue;
        clearVenue.title = tr("Remove the venue from this layout?");
        clearVenue.removes = tr("The venue’s outline, walls, doors and obstacles leave this layout.");
        clearVenue.keeps = tr("Your parts stay where they are, and the venue stays in the Venue library if you saved it there.");
        clearVenue.undo = ConfirmDialog::undoWithCtrlZ();
        clearVenue.confirmLabel = tr("Remove");
        if (ConfirmDialog::ask(this, clearVenue)) {
            result_.reset();
            cleared_ = true;
            accept();
        }
    });
}

}
