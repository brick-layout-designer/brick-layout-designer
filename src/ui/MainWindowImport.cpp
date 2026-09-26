// Tools → Import: LDraw / Studio / LDD models → library parts, one at a
// time through the preview dialog or many at once (Batch Import). The
// heavy lifting lives in ImportPipeline; this file is the UI around it.

#include "MainWindow.h"
#include "BackgroundTask.h"
#include "ImportPipeline.h"
#include "ImportPreviewDialog.h"
#include "MapView.h"
#include "../core/Map.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>

namespace bld::ui {

namespace {

const QString kLastCategoryKey = QStringLiteral("import/lastCategory");

ImportSettings currentImportSettings() {
    QSettings s;
    ImportSettings out;
    out.ldrawLibrary = s.value(QStringLiteral("import/ldrawLibraryPath")).toString();
    out.studioLibrary = s.value(QStringLiteral("import/studioLibraryPath")).toString();
    out.lddPath      = s.value(QStringLiteral("import/lddInstallPath")).toString();
    out.lddLdrawXml  = s.value(QStringLiteral("import/lddLdrawXml")).toString();
    return out;
}

// The imports root is itself the "imports" category; any other category
// is a sub-folder of it (the Parts panel names categories by folder).
QString categoryDir(const QString& importsRoot, const QString& category) {
    return category.isEmpty() || category == QDir(importsRoot).dirName()
        ? importsRoot
        : QDir(importsRoot).filePath(category);
}

QStringList importCategories(const QString& importsRoot) {
    QStringList out{ QDir(importsRoot).dirName() };
    const QDir root(importsRoot);
    for (const QString& sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        out << sub;
    }
    return out;
}

bool importedPartExists(const QString& importsRoot, const QString& name, const QString& category) {
    return QFileInfo::exists(QDir(categoryDir(importsRoot, category))
                                 .filePath(import::importedPartKey(name) + QStringLiteral(".xml")));
}

}  // namespace

void MainWindow::importModelFile(const QString& path) {
    const HeavyRunner runHeavy = [this](const QString& label,
                                        const std::function<void(CancelToken&)>& work) {
        return runBackground(this, label, work);
    };
    PreparedPart part = prepareImport(path, currentImportSettings(), parts_, runHeavy);
    if (!part.ok()) {
        if (part.cancelled)
            statusBar()->showMessage(part.error, 3000);
        else
            QMessageBox::warning(this, part.kindLabel, part.error);
        return;
    }

    const QString root = importedPartsRoot();
    const QString lastCategory = QSettings().value(kLastCategoryKey, QDir(root).dirName()).toString();
    ImportPreviewDialog dlg(std::move(part), importCategories(root), lastCategory,
        [root](const QString& name, const QString& category) {
            return importedPartExists(root, name, category);
        }, this);
    if (dlg.exec() != QDialog::Accepted) {
        statusBar()->showMessage(tr("Import cancelled."), 3000);
        return;
    }
    QSettings().setValue(kLastCategoryKey, dlg.category());

    const PreparedPart result = dlg.result();
    const QString author = mapView_->currentMap() ? mapView_->currentMap()->author : QString();
    QString err;
    const QString dir = categoryDir(root, dlg.category());
    const QString key = writeImportedPart(result, dlg.partName(), dir, author,
                                          dlg.replaceExisting(), &err);
    if (key.isEmpty()) {
        QMessageBox::warning(this, result.kindLabel, tr("Could not write library part: %1").arg(err));
        return;
    }
    const QString libKey = registerImportedPart(QDir(dir).filePath(key + QStringLiteral(".xml")));
    if (!libKey.isEmpty()) mapView_->addPartAtViewCenter(libKey);
    statusBar()->showMessage(
        tr("Imported %1 as '%2' (%3 × %4 studs, %n connection point(s))", nullptr,
           result.connections.size())
            .arg(QFileInfo(path).fileName(), key)
            .arg(result.widthStuds).arg(result.heightStuds), 8000);
}

void MainWindow::onBatchImport() {
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Batch Import"), {},
        tr("Models (*.ldr *.dat *.mpd *.io *.lxf *.lxfml);;All files (*)"));
    if (files.isEmpty()) return;

    // Where they go.
    const QString root = importedPartsRoot();
    QDialog opts(this);
    opts.setWindowTitle(tr("Batch Import"));
    auto* form = new QFormLayout(&opts);
    form->addRow(new QLabel(tr("Import %n file(s) as library parts.", nullptr, files.size()), &opts));
    auto* category = new QComboBox(&opts);
    category->setEditable(true);
    category->addItems(importCategories(root));
    category->setCurrentText(QSettings().value(kLastCategoryKey, QDir(root).dirName()).toString());
    form->addRow(tr("Category:"), category);
    auto* replace = new QCheckBox(tr("Replace existing parts with the same name"), &opts);
    replace->setToolTip(tr("Otherwise a copy is saved with a -2, -3 ... suffix"));
    form->addRow(QString(), replace);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &opts);
    connect(bb, &QDialogButtonBox::accepted, &opts, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &opts, &QDialog::reject);
    form->addRow(bb);
    if (opts.exec() != QDialog::Accepted || category->currentText().trimmed().isEmpty()) return;
    const QString cat = category->currentText().trimmed();
    QSettings().setValue(kLastCategoryKey, cat);
    const QString dir = categoryDir(root, cat);

    const ImportSettings settings = currentImportSettings();
    const QString author = mapView_->currentMap() ? mapView_->currentMap()->author : QString();
    QProgressDialog progress(tr("Importing..."), tr("Cancel"), 0, static_cast<int>(files.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    CancelToken token;
    connect(&progress, &QProgressDialog::canceled, this, [&token]{ token.cancel(); });
    const HeavyRunner runHeavy = [&token](const QString&, const std::function<void(CancelToken&)>& work) {
        return runOnWorker(token, work);
    };

    QStringList done, failed;
    for (int i = 0; i < files.size() && !token.requested(); ++i) {
        const QString name = QFileInfo(files[i]).fileName();
        progress.setLabelText(tr("Importing %1 (%2 of %3)...").arg(name).arg(i + 1).arg(files.size()));
        progress.setValue(i);
        const PreparedPart part = prepareImport(files[i], settings, parts_, runHeavy);
        if (token.requested()) break;
        if (!part.ok()) {
            failed << tr("%1: %2").arg(name, part.error);
            continue;
        }
        QString err;
        const QString key = writeImportedPart(part, name, dir, author, replace->isChecked(), &err);
        if (key.isEmpty()) {
            failed << tr("%1: %2").arg(name, err);
            continue;
        }
        registerImportedPart(QDir(dir).filePath(key + QStringLiteral(".xml")));
        done << tr("%1 → %2 (%3 × %4 studs, %n connection point(s))", nullptr, part.connections.size())
                    .arg(name, key).arg(part.widthStuds).arg(part.heightStuds);
    }
    progress.setValue(static_cast<int>(files.size()));

    const int skipped = static_cast<int>(files.size()) - done.size() - failed.size();
    QMessageBox box(failed.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                    tr("Batch Import"),
                    tr("Imported %1 of %2 file(s) into “%3”.").arg(done.size()).arg(files.size()).arg(cat),
                    QMessageBox::Ok, this);
    QStringList details;
    if (!done.isEmpty())   details << tr("Imported:") << done;
    if (!failed.isEmpty()) details << QString() << tr("Failed:") << failed;
    if (skipped > 0)       details << QString() << tr("%n file(s) skipped (cancelled).", nullptr, skipped);
    box.setDetailedText(details.join(QLatin1Char('\n')));
    box.exec();
}

}  // namespace bld::ui
