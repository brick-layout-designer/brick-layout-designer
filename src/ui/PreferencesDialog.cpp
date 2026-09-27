#include "PreferencesDialog.h"
#include "LibraryPathsDialog.h"
#include "../import/ArchivePath.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QUrl>
#include <QVBoxLayout>

#include "../import/zip/SafeZip.h"

namespace bld::ui {

namespace {

QWidget* buildGeneralTab(QDialog* parent) {
    auto* w = new QWidget(parent);
    auto* form = new QFormLayout(w);
    QSettings s;

    auto* undoSpin = new QSpinBox(w);
    undoSpin->setRange(10, 10000);
    undoSpin->setValue(s.value(QStringLiteral("general/undoStackDepth"), 500).toInt());
    form->addRow(QObject::tr("Undo stack depth:"), undoSpin);

    auto* wheelSpin = new QDoubleSpinBox(w);
    wheelSpin->setRange(1.05, 2.00); wheelSpin->setDecimals(3); wheelSpin->setSingleStep(0.05);
    wheelSpin->setValue(s.value(QStringLiteral("general/wheelZoomFactor"), 1.20).toDouble());
    form->addRow(QObject::tr("Mouse wheel zoom factor:"), wheelSpin);

    auto* reopenChk = new QCheckBox(QObject::tr("Reopen last file at launch"), w);
    reopenChk->setChecked(s.value(QStringLiteral("general/reopenLastFile"), true).toBool());
    form->addRow(reopenChk);

    auto* splashChk = new QCheckBox(QObject::tr("Show splash screen at startup"), w);
    splashChk->setChecked(s.value(QStringLiteral("general/showSplash"), false).toBool());
    form->addRow(splashChk);

    // BlueBrick's "default budget" choice: parts a budget doesn't list.
    auto* budgetInfiniteChk = new QCheckBox(QObject::tr("Parts without a budget are unlimited (otherwise: not allowed)"), w);
    budgetInfiniteChk->setChecked(s.value(QStringLiteral("budget/defaultInfinite"), true).toBool());
    form->addRow(budgetInfiniteChk);

    // Template file for File > New — vanilla "use default template" parity.
    auto* tplEdit = new QLineEdit(
        s.value(QStringLiteral("general/newMapTemplate")).toString(), w);
    auto* tplBrowse = new QPushButton(QObject::tr("..."), w);
    QObject::connect(tplBrowse, &QPushButton::clicked, w, [tplEdit, w]{
        const QString p = QFileDialog::getOpenFileName(w,
            QObject::tr("New-map template"), tplEdit->text(),
            QObject::tr("BlueBrick map (*.bbm)"));
        if (!p.isEmpty()) tplEdit->setText(p);
    });
    auto* tplRow = new QHBoxLayout();
    tplRow->addWidget(tplEdit, 1);
    tplRow->addWidget(tplBrowse);
    auto* tplWrap = new QWidget(w); tplWrap->setLayout(tplRow);
    form->addRow(QObject::tr("File > New template:"), tplWrap);

    // Language selector (scaffolding; applies next launch).
    auto* langCombo = new QComboBox(w);
    langCombo->addItem(QObject::tr("English"),     QStringLiteral("en"));
    langCombo->addItem(QObject::tr("Français"),    QStringLiteral("fr"));
    langCombo->addItem(QObject::tr("Deutsch"),     QStringLiteral("de"));
    langCombo->addItem(QObject::tr("Nederlands"),  QStringLiteral("nl"));
    langCombo->addItem(QObject::tr("Português"),   QStringLiteral("pt"));
    langCombo->addItem(QObject::tr("Español"),     QStringLiteral("es"));
    langCombo->addItem(QObject::tr("Italiano"),    QStringLiteral("it"));
    langCombo->addItem(QObject::tr("Norsk"),       QStringLiteral("no"));
    langCombo->addItem(QObject::tr("Svenska"),     QStringLiteral("sv"));
    langCombo->addItem(QStringLiteral("中文"),       QStringLiteral("zh"));
    langCombo->addItem(QStringLiteral("日本語"),     QStringLiteral("ja"));
    const QString curLang = s.value(QStringLiteral("general/language"), QStringLiteral("en")).toString();
    for (int i = 0; i < langCombo->count(); ++i)
        if (langCombo->itemData(i).toString() == curLang) { langCombo->setCurrentIndex(i); break; }
    form->addRow(QObject::tr("Language (restart required):"), langCombo);

    // Save-on-accept: parent's accepted signal fires before exec() returns, so
    // we wire per-tab savers that the dialog's QDialogButtonBox can trigger.
    QObject::connect(parent, &QDialog::accepted, w, [undoSpin, wheelSpin, reopenChk, splashChk, budgetInfiniteChk, tplEdit, langCombo]{
        QSettings s;
        s.setValue(QStringLiteral("general/undoStackDepth"), undoSpin->value());
        s.setValue(QStringLiteral("general/wheelZoomFactor"), wheelSpin->value());
        s.setValue(QStringLiteral("general/reopenLastFile"), reopenChk->isChecked());
        s.setValue(QStringLiteral("general/showSplash"), splashChk->isChecked());
        s.setValue(QStringLiteral("budget/defaultInfinite"), budgetInfiniteChk->isChecked());
        s.setValue(QStringLiteral("general/newMapTemplate"), tplEdit->text());
        s.setValue(QStringLiteral("general/language"), langCombo->currentData().toString());
    });
    return w;
}

QWidget* buildEditionTab(QDialog* parent) {
    auto* w = new QWidget(parent);
    auto* form = new QFormLayout(w);
    QSettings s;
    s.beginGroup(QStringLiteral("editing"));

    auto* snapCombo = new QComboBox(w);
    for (const auto& p : std::vector<std::pair<QString,double>>{
            { QObject::tr("off"), 0.0 }, { QStringLiteral("32"), 32.0 },
            { QStringLiteral("16"), 16.0 }, { QStringLiteral("8"),  8.0 },
            { QStringLiteral("4"),  4.0 }, { QStringLiteral("2"),  2.0 },
            { QStringLiteral("1"),  1.0 }, { QStringLiteral("0.5"), 0.5 } }) {
        snapCombo->addItem(p.first, p.second);
    }
    const double curSnap = s.value(QStringLiteral("snapStepStuds"), 0.0).toDouble();
    for (int i = 0; i < snapCombo->count(); ++i)
        if (qFuzzyCompare(snapCombo->itemData(i).toDouble() + 1.0, curSnap + 1.0))
            snapCombo->setCurrentIndex(i);
    form->addRow(QObject::tr("Default snap step (studs):"), snapCombo);

    auto* rotCombo = new QComboBox(w);
    for (double d : { 90.0, 45.0, 22.5, 11.25, 5.0, 1.0 })
        rotCombo->addItem(QString::number(d) + QStringLiteral("°"), d);
    const double curRot = s.value(QStringLiteral("rotationStepDegrees"), 90.0).toDouble();
    for (int i = 0; i < rotCombo->count(); ++i)
        if (qFuzzyCompare(rotCombo->itemData(i).toDouble(), curRot))
            rotCombo->setCurrentIndex(i);
    form->addRow(QObject::tr("Default rotation step:"), rotCombo);

    QColor paintColor(s.value(QStringLiteral("paintColor"), QColor(0, 128, 0).name()).toString());
    auto* colorBtn = new QPushButton(w);
    auto refreshColor = [colorBtn, &paintColor]{
        QPixmap pm(24, 16); pm.fill(paintColor);
        colorBtn->setIcon(QIcon(pm));
        colorBtn->setText(paintColor.name(QColor::HexArgb));
    };
    refreshColor();
    QObject::connect(colorBtn, &QPushButton::clicked, colorBtn, [&paintColor, refreshColor, w]{
        QColor c = QColorDialog::getColor(paintColor, w, QObject::tr("Default paint colour"),
                                          QColorDialog::ShowAlphaChannel);
        if (c.isValid()) { paintColor = c; refreshColor(); }
    });
    form->addRow(QObject::tr("Default paint colour:"), colorBtn);

    s.endGroup();

    QObject::connect(parent, &QDialog::accepted, w, [snapCombo, rotCombo, &paintColor]{
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("snapStepStuds"), snapCombo->currentData().toDouble());
        s.setValue(QStringLiteral("rotationStepDegrees"), rotCombo->currentData().toDouble());
        s.setValue(QStringLiteral("paintColor"), paintColor.name(QColor::HexArgb));
        s.endGroup();
    });
    return w;
}

QWidget* buildAppearanceTab(QDialog* parent) {
    auto* w = new QWidget(parent);
    auto* form = new QFormLayout(w);
    QSettings s;

    auto* gridChk = new QCheckBox(QObject::tr("Show grid"), w);
    gridChk->setChecked(s.value(QStringLiteral("appearance/showGrid"), true).toBool());
    form->addRow(gridChk);

    auto* connDotsChk = new QCheckBox(QObject::tr("Always show connection points (not only on selection)"), w);
    connDotsChk->setChecked(s.value(QStringLiteral("appearance/alwaysShowConnections"), false).toBool());
    form->addRow(connDotsChk);

    auto* highlightChk = new QCheckBox(QObject::tr("Selection tint (clearly-coloured overlay)"), w);
    highlightChk->setChecked(s.value(QStringLiteral("appearance/selectionTint"), true).toBool());
    form->addRow(highlightChk);

    auto* watermarkChk = new QCheckBox(QObject::tr("Show general-info watermark on export"), w);
    watermarkChk->setChecked(s.value(QStringLiteral("appearance/exportWatermark"), true).toBool());
    form->addRow(watermarkChk);

    auto* moduleFrameSpin = new QDoubleSpinBox(w);
    moduleFrameSpin->setRange(0.5, 20.0);
    moduleFrameSpin->setSingleStep(0.5);
    moduleFrameSpin->setDecimals(1);
    moduleFrameSpin->setSuffix(QObject::tr(" px"));
    moduleFrameSpin->setValue(
        s.value(QStringLiteral("view/moduleFrameThickness"), 5.0).toDouble());
    form->addRow(QObject::tr("Module frame thickness:"), moduleFrameSpin);

    auto* moduleLabelSpin = new QDoubleSpinBox(w);
    moduleLabelSpin->setRange(5.0, 100.0);
    moduleLabelSpin->setSingleStep(5.0);
    moduleLabelSpin->setDecimals(0);
    moduleLabelSpin->setSuffix(QObject::tr(" %"));
    moduleLabelSpin->setValue(
        s.value(QStringLiteral("view/moduleLabelPercent"), 35.0).toDouble());
    moduleLabelSpin->setToolTip(QObject::tr(
        "Module name size as a percentage of the module's long axis. "
        "Higher = bigger label. Scene rebuilds on dialog close."));
    form->addRow(QObject::tr("Module label size:"), moduleLabelSpin);

    auto* venueLabelSpin = new QSpinBox(w);
    venueLabelSpin->setRange(10, 96);
    venueLabelSpin->setSingleStep(2);
    venueLabelSpin->setSuffix(QObject::tr(" px"));
    venueLabelSpin->setValue(
        s.value(QStringLiteral("venue/labelPx"), 28).toInt());
    venueLabelSpin->setToolTip(QObject::tr(
        "Font size for venue edge labels (wall / door / open segment "
        "measurements). Pixel size — stays legible at the typical venue "
        "zoom. Scene rebuilds on dialog close."));
    form->addRow(QObject::tr("Venue label size:"), venueLabelSpin);

    QObject::connect(parent, &QDialog::accepted, w,
        [gridChk, connDotsChk, highlightChk, watermarkChk,
         moduleFrameSpin, moduleLabelSpin, venueLabelSpin]{
            QSettings s;
            s.setValue(QStringLiteral("appearance/showGrid"), gridChk->isChecked());
            s.setValue(QStringLiteral("appearance/alwaysShowConnections"), connDotsChk->isChecked());
            s.setValue(QStringLiteral("appearance/selectionTint"), highlightChk->isChecked());
            s.setValue(QStringLiteral("appearance/exportWatermark"), watermarkChk->isChecked());
            s.setValue(QStringLiteral("view/moduleFrameThickness"), moduleFrameSpin->value());
            s.setValue(QStringLiteral("view/moduleLabelPercent"),   moduleLabelSpin->value());
            s.setValue(QStringLiteral("venue/labelPx"),             venueLabelSpin->value());
        });
    return w;
}

QWidget* buildLibraryTab(QDialog* parent) {
    auto* w = new QWidget(parent);
    auto* vbox = new QVBoxLayout(w);
    QSettings s;

    auto* moduleDirLabel = new QLabel(QObject::tr("Module library folder:"), w);
    auto* moduleDirEdit = new QLineEdit(w);
    moduleDirEdit->setText(s.value(QStringLiteral("modules/libraryPath")).toString());
    auto* moduleBrowse = new QPushButton(QObject::tr("Browse..."), w);
    QObject::connect(moduleBrowse, &QPushButton::clicked, w, [moduleDirEdit, w]{
        const QString p = QFileDialog::getExistingDirectory(w, QObject::tr("Module library folder"));
        if (!p.isEmpty()) moduleDirEdit->setText(p);
    });
    auto* modRow = new QHBoxLayout();
    modRow->addWidget(moduleDirEdit, 1);
    modRow->addWidget(moduleBrowse);
    vbox->addWidget(moduleDirLabel);
    vbox->addLayout(modRow);

    auto* libBox = new QGroupBox(QObject::tr("Additional parts library paths"), w);
    auto* libVbox = new QVBoxLayout(libBox);
    auto* list = new QListWidget(libBox);
    s.beginGroup(LibraryPathsDialog::kSettingsGroup);
    for (const QString& p : s.value(LibraryPathsDialog::kSettingsKey).toStringList()) list->addItem(p);
    s.endGroup();
    libVbox->addWidget(list);
    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(QObject::tr("Add..."), libBox);
    auto* rmBtn  = new QPushButton(QObject::tr("Remove"), libBox);
    btnRow->addWidget(addBtn); btnRow->addWidget(rmBtn); btnRow->addStretch();
    libVbox->addLayout(btnRow);
    QObject::connect(addBtn, &QPushButton::clicked, w, [list, w]{
        const QString p = QFileDialog::getExistingDirectory(w, QObject::tr("Add parts library path"));
        if (!p.isEmpty()) list->addItem(p);
    });
    QObject::connect(rmBtn, &QPushButton::clicked, list, [list]{
        qDeleteAll(list->selectedItems());
    });
    vbox->addWidget(libBox, 1);

    QObject::connect(parent, &QDialog::accepted, w, [moduleDirEdit, list]{
        QSettings s;
        s.setValue(QStringLiteral("modules/libraryPath"), moduleDirEdit->text());
        QStringList paths;
        for (int i = 0; i < list->count(); ++i) paths << list->item(i)->text();
        s.beginGroup(LibraryPathsDialog::kSettingsGroup);
        s.setValue(LibraryPathsDialog::kSettingsKey, paths);
        s.endGroup();
    });
    return w;
}

// "Import" tab — paths to external libraries used by the LDraw /
// Studio / LDD import pipeline. These are read at import time to
// resolve part geometry against the user's own LDraw install or LDD
// install rather than the bundled BlueBrickParts library (which
// doesn't have the geometry needed for arbitrary LDraw / LDD models).
QWidget* buildImportTab(QDialog* parent) {
    auto* w = new QWidget(parent);
    auto* form = new QFormLayout(w);
    QSettings s;

    auto buildPicker = [w](const QString& settingsKey, const QString& browseTitle) {
        auto* edit = new QLineEdit(QSettings().value(settingsKey).toString(), w);
        auto* btn  = new QPushButton(QObject::tr("Browse..."), w);
        QObject::connect(btn, &QPushButton::clicked, w, [edit, w, browseTitle]{
            const QString p = QFileDialog::getExistingDirectory(w, browseTitle, edit->text());
            if (!p.isEmpty()) edit->setText(p);
        });
        auto* row = new QHBoxLayout();
        row->addWidget(edit, 1);
        row->addWidget(btn);
        auto* wrap = new QWidget(w); wrap->setLayout(row);
        return std::make_pair(wrap, edit);
    };

    auto [ldrawWrap, ldrawEdit] = buildPicker(
        QStringLiteral("import/ldrawLibraryPath"),
        QObject::tr("LDraw library root"));
    ldrawEdit->setPlaceholderText(QObject::tr("e.g. ~/ldraw — needs LDConfig.ldr + parts/"));
    form->addRow(QObject::tr("LDraw library root:"), ldrawWrap);

    // One-click LDraw library install. Downloads the official complete
    // archive from library.ldraw.org, extracts it under the user's
    // app-data folder, and points the library-root field at the
    // resulting `ldraw/` directory. Saves the user the maintenance of
    // tracking the LDraw release page themselves.
    auto* autoBtn = new QPushButton(
        QObject::tr("Download LDraw library automatically..."), w);
    autoBtn->setToolTip(QObject::tr(
        "Fetches library.ldraw.org/library/updates/complete.zip (~140 MB), "
        "extracts under your app-data folder, and configures the path. "
        "Requires an internet connection."));
    QObject::connect(autoBtn, &QPushButton::clicked, w, [ldrawEdit, w]{
        const auto btn = QMessageBox::question(w,
            QObject::tr("Download LDraw library"),
            QObject::tr("This will download ~140 MB from library.ldraw.org "
                        "and extract it to your app-data folder. Continue?"));
        if (btn != QMessageBox::Yes) return;

        const QString destRoot = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + QStringLiteral("/ldraw-library");
        QDir().mkpath(destRoot);

        // QNetworkAccessManager owned by the heap so its lifetime
        // outlives the lambda capture if the user closes the
        // Preferences dialog mid-download. parent = w so it gets
        // cleaned up with the widget.
        auto* nam = new QNetworkAccessManager(w);
        QNetworkRequest req(QUrl(QStringLiteral(
            "https://library.ldraw.org/library/updates/complete.zip")));
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Brick-Layout-Designer/1.0"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* rep = nam->get(req);

        // Modal-but-pumping progress dialog. setValue() pumps the
        // event loop on its own — so as long as we keep calling
        // setValue() we never block. The cancel button is wired
        // before the network reply gets to processEvents-blocking
        // state so the user can always escape.
        QProgressDialog dlg(QObject::tr("Downloading LDraw library..."),
                             QObject::tr("Cancel"), 0, 100, w);
        dlg.setWindowModality(Qt::WindowModal);
        dlg.setMinimumDuration(0);
        dlg.setAutoClose(false);
        dlg.setAutoReset(false);
        bool cancelled = false;
        QObject::connect(&dlg, &QProgressDialog::canceled, rep,
            [rep, &cancelled]{ cancelled = true; rep->abort(); });
        QObject::connect(rep, &QNetworkReply::downloadProgress, &dlg,
            [&dlg](qint64 done, qint64 total){
                if (total <= 0) { dlg.setRange(0, 0); return; }
                const int mb = std::max(1, static_cast<int>(total / (1024 * 1024)));
                dlg.setMaximum(mb);
                dlg.setValue(static_cast<int>(done / (1024 * 1024)));
                dlg.setLabelText(QObject::tr("Downloading LDraw library — %1 / %2 MB")
                    .arg(done / (1024 * 1024)).arg(mb));
            });
        QEventLoop loop;
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        // Show explicitly so it appears immediately rather than after
        // the minimum-duration delay.
        dlg.show();
        loop.exec();

        if (cancelled) {
            dlg.close();
            rep->deleteLater();
            nam->deleteLater();
            return;
        }
        if (rep->error() != QNetworkReply::NoError) {
            dlg.close();
            QMessageBox::warning(w, QObject::tr("Download failed"), rep->errorString());
            rep->deleteLater();
            nam->deleteLater();
            return;
        }
        QByteArray bytes = rep->readAll();
        rep->deleteLater();
        nam->deleteLater();
        if (bytes.isEmpty()) { dlg.close(); return; }

        // One file at a time so the progress dialog stays alive and
        // cancellable: the LDraw archive has ~20 000 entries.
        const import::SafeZip zip(bytes);
        if (!zip.isValid()) {
            dlg.close();
            QMessageBox::warning(w, QObject::tr("Extract failed"),
                QObject::tr("Couldn't read the LDraw archive."));
            return;
        }
        const auto& entries = zip.entries();
        dlg.setRange(0, entries.size());
        dlg.setValue(0);
        dlg.setLabelText(QObject::tr("Extracting %1 files...").arg(entries.size()));
        QApplication::processEvents();

        QDir dst(destRoot);
        cancelled = false;
        QObject::disconnect(&dlg, &QProgressDialog::canceled, nullptr, nullptr);
        QObject::connect(&dlg, &QProgressDialog::canceled, &dlg,
            [&cancelled]{ cancelled = true; });

        int failed = 0;
        for (int i = 0; i < entries.size(); ++i) {
            if (cancelled) break;
            const auto& entry = entries[i];
            // Drops "../" / absolute entries (zip-slip) and never recreates
            // symlink entries, which could redirect later writes.
            const QString abs = import::resolveArchiveEntryPath(dst.absolutePath(), entry.name);
            if (abs.isEmpty() || entry.isSymLink) {
                ++failed;
            } else if (entry.isDir) {
                QDir().mkpath(abs);
            } else {
                const auto data = zip.read(entry);
                QDir().mkpath(QFileInfo(abs).absolutePath());
                QFile out(abs);
                if (!data || !out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(*data) < 0)
                    ++failed;
            }
            // Update + pump every ~64 files so the dialog stays
            // responsive without crawling the whole loop with
            // synchronous event pumps.
            if ((i & 63) == 0) {
                dlg.setValue(i);
                QApplication::processEvents();
            }
        }
        dlg.setValue(entries.size());
        if (cancelled) {
            dlg.close();
            QMessageBox::information(w, QObject::tr("Download cancelled"),
                QObject::tr("Extraction cancelled — partial files may remain at %1")
                    .arg(destRoot));
            return;
        }
        dlg.close();

        // The archive expands to <destRoot>/ldraw/parts, /p, LDConfig.ldr —
        // point the path field one level deeper.
        const QString ldrawRoot = QDir(destRoot).filePath(QStringLiteral("ldraw"));
        if (QFileInfo::exists(QDir(ldrawRoot).filePath(QStringLiteral("LDConfig.ldr")))) {
            ldrawEdit->setText(ldrawRoot);
        } else {
            // Fallback: if the archive happens to be flat (no top
            // ldraw/ wrapper), use destRoot directly.
            ldrawEdit->setText(destRoot);
        }
        QString msg = QObject::tr("LDraw library installed at:\n%1").arg(ldrawEdit->text());
        if (failed > 0) {
            msg += QStringLiteral("\n\n")
                 + QObject::tr("Archive entries not written (write error or unsafe path): %1")
                       .arg(failed);
        }
        QMessageBox::information(w, QObject::tr("LDraw library installed"), msg);
    });
    form->addRow(QString(), autoBtn);

    auto [studioWrap, studioEdit] = buildPicker(
        QStringLiteral("import/studioLibraryPath"),
        QObject::tr("Studio LDraw library root"));
    studioEdit->setPlaceholderText(
        QObject::tr("Studio's bundled LDraw — falls back to LDraw library above"));
    form->addRow(QObject::tr("Studio library root:"), studioWrap);

    auto [lddWrap, lddEdit] = buildPicker(
        QStringLiteral("import/lddInstallPath"),
        QObject::tr("LEGO Digital Designer install"));
    lddEdit->setPlaceholderText(
        QObject::tr("LDD program folder (Assets.lif) or data folder (db.lif)"));
    form->addRow(QObject::tr("LDD install:"), lddWrap);

    // LDD ships an old ldraw.xml without e.g. train track; the community
    // keeps a much larger one up to date.
    auto* xmlEdit = new QLineEdit(QSettings().value(QStringLiteral("import/lddLdrawXml")).toString(), w);
    xmlEdit->setPlaceholderText(QObject::tr("optional newer ldraw.xml, e.g. from slswww.free.fr/ldraw.xml"));
    auto* xmlBtn = new QPushButton(QObject::tr("Browse..."), w);
    QObject::connect(xmlBtn, &QPushButton::clicked, w, [xmlEdit, w]{
        const QString p = QFileDialog::getOpenFileName(w, QObject::tr("LDD → LDraw mapping"),
            xmlEdit->text(), QObject::tr("ldraw.xml (*.xml)"));
        if (!p.isEmpty()) xmlEdit->setText(p);
    });
    auto* xmlRow = new QHBoxLayout();
    xmlRow->addWidget(xmlEdit, 1);
    xmlRow->addWidget(xmlBtn);
    auto* xmlWrap = new QWidget(w);
    xmlWrap->setLayout(xmlRow);
    form->addRow(QObject::tr("LDD → LDraw mapping:"), xmlWrap);

    QObject::connect(parent, &QDialog::accepted, w, [ldrawEdit, studioEdit, lddEdit, xmlEdit]{
        QSettings s;
        s.setValue(QStringLiteral("import/ldrawLibraryPath"),  ldrawEdit->text());
        s.setValue(QStringLiteral("import/studioLibraryPath"), studioEdit->text());
        s.setValue(QStringLiteral("import/lddInstallPath"),    lddEdit->text());
        s.setValue(QStringLiteral("import/lddLdrawXml"),       xmlEdit->text());
    });
    return w;
}

}  // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Preferences"));
    resize(600, 480);

    auto* vbox = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralTab(this),    tr("General"));
    tabs->addTab(buildEditionTab(this),    tr("Editing"));
    tabs->addTab(buildAppearanceTab(this), tr("Appearance"));
    tabs->addTab(buildLibraryTab(this),    tr("Library"));
    tabs->addTab(buildImportTab(this),     tr("Import"));
    vbox->addWidget(tabs);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    vbox->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

}
