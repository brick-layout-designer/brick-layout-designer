// Tools → Import: LDraw / Studio / LDD models → library parts, one at a
// time through the preview dialog or many at once (Batch Import). The
// heavy lifting lives in ImportPipeline; this file is the UI around it.
// An imported part can be made again later: from its original file
// (Re-import), from another file (Replace with Another File), or by
// importing a model under a name that's already taken (Replace).

#include "MainWindow.h"
#include "BackgroundTask.h"
#include "ImportPipeline.h"
#include "ImportPreviewDialog.h"
#include "LiveLayout.h"
#include "MapView.h"
#include "NoticeArea.h"
#include "ServerLibrary.h"
#include "help/HelpButton.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

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

// The files Import opens, all together and by kind.
QString modelFileFilter() {
    return MainWindow::tr("Models (*.ldr *.dat *.mpd *.io *.lxf *.lxfml);;LDraw (*.ldr *.dat *.mpd);;"
                          "Studio (*.io);;LDD (*.lxf *.lxfml);;All files (*)");
}

// The name people see for an imported part: its English description,
// unless that is only the importer's own "Imported from ...".
QString shownName(const parts::PartMetadata& meta) {
    QString shown;
    for (const auto& d : meta.descriptions)
        if (d.language == QLatin1String("en")) shown = d.text;
    if (shown.startsWith(QLatin1String("Imported from "))) shown.clear();
    return shown.isEmpty() ? QFileInfo(meta.xmlFilePath).completeBaseName() : shown;
}

}  // namespace

QString MainWindow::importClashWith(const QString& name) const {
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) return {};
    const QString key = import::importedPartKey(wanted);
    // A batch names parts after their files: "Station.ldr" is "Station".
    static const QRegularExpression modelExt(QStringLiteral("\\.(ldr|dat|mpd|io|lxf|lxfml)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    const QString plain = QString(wanted).remove(modelExt);
    QString byName;
    for (const QString& k : parts_.keys()) {
        const auto meta = parts_.metadata(k);
        if (!meta || !meta->importSource) continue;
        // The part number it would get is that part's.
        if (k.compare(key, Qt::CaseInsensitive) == 0) return k;
        if (byName.isEmpty() && shownName(*meta).compare(plain, Qt::CaseInsensitive) == 0) byName = k;
    }
    return byName;
}

QString MainWindow::freeImportKey(const QString& name, const QString& dir) const {
    const QString base = import::importedPartKey(name);
    const auto taken = [&](const QString& k) {
        if (parts_.metadata(k)) return true;
        for (const char* ext : { ".xml", ".png", ".gif" })
            if (QFileInfo::exists(QDir(dir).filePath(k + QLatin1String(ext)))) return true;
        return false;
    };
    QString key = base;
    for (int n = 2; taken(key); ++n) key = base + QStringLiteral("-") + QString::number(n);
    return key;
}

std::optional<QString> MainWindow::askImportClash(const QString& name, const QString& key, int rest, bool* applyToAll) {
    if (applyToAll) *applyToAll = false;
    QMessageBox box(QMessageBox::Question, tr("Import"),
                    tr("You already have a part called “%1”.").arg(name), QMessageBox::NoButton, this);
    box.setInformativeText(tr("Replace it with this model, or keep both? Replacing it changes the part "
                              "everywhere it is used. Keeping both adds this one as a new part."));
    auto* replace = box.addButton(tr("Replace the existing part"), QMessageBox::YesRole);
    replace->setObjectName(QStringLiteral("clashReplace"));
    auto* both = box.addButton(tr("Keep both"), QMessageBox::NoRole);
    both->setObjectName(QStringLiteral("clashKeepBoth"));
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(both);
    QCheckBox* all = nullptr;
    if (rest > 0) {
        all = new QCheckBox(tr("Do the same for the other %n part(s) you already have", nullptr, rest), &box);
        box.setCheckBox(all);
    }
    help::addToMessageBox(&box, QStringLiteral("import.clash"));
    box.exec();
    if (box.clickedButton() == replace) {
        if (applyToAll) *applyToAll = all && all->isChecked();
        return key;
    }
    if (box.clickedButton() == both) {
        if (applyToAll) *applyToAll = all && all->isChecked();
        return QString();
    }
    return std::nullopt;
}

QString MainWindow::writeOverImportedPart(const PreparedPart& part, const QString& key, const QString& name,
                                          QString* error) {
    const auto meta = parts_.metadata(key);
    if (!meta || meta->xmlFilePath.isEmpty()) {
        if (error) *error = tr("the part “%1” isn't in your parts any more").arg(key);
        return {};
    }
    // Same part number (as its file is spelled), same folder: the category.
    const QFileInfo xml(meta->xmlFilePath);
    const QString dir = xml.absolutePath();
    const QString author = mapView_->currentMap() ? mapView_->currentMap()->author : QString();
    QString written = writeImportedPart(part, name, dir, author, /*replaceExisting=*/true, error,
                                        xml.completeBaseName());
    if (written.isEmpty()) return {};
    // The old part's picture is forgotten and every copy on the map redrawn.
    registerImportedPart(QDir(dir).filePath(written + QStringLiteral(".xml")));
    return written;
}

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
    // A name that's taken is asked about after the preview (replace or keep both).
    ImportPreviewDialog dlg(std::move(part), importCategories(root), lastCategory, {}, this);
    if (dlg.exec() != QDialog::Accepted) {
        statusBar()->showMessage(tr("Import cancelled."), 3000);
        return;
    }
    QSettings().setValue(kLastCategoryKey, dlg.category());

    const PreparedPart result = dlg.result();
    const QString name = dlg.partName();
    QString err;
    if (const QString clash = importClashWith(name); !clash.isEmpty()) {
        const auto choice = askImportClash(name, clash, 0, nullptr);
        if (!choice) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return;
        }
        if (!choice->isEmpty()) {
            const bool used = mapView_->currentMap() && sync::partNumbersIn(*mapView_->currentMap()).contains(choice->toUpper());
            const QString key = writeOverImportedPart(result, *choice, name, &err);
            if (key.isEmpty()) {
                QMessageBox::warning(this, result.kindLabel, tr("Could not save the custom part: %1").arg(err));
                return;
            }
            // Already in the layout: it shows the new model where it is.
            if (!used) mapView_->addPartAtViewCenter(key);
            statusBar()->showMessage(tr("Replaced “%1” with %2").arg(key, QFileInfo(path).fileName()), 8000);
            offerToUpdateServerParts({ key });
            return;
        }
    }
    const QString author = mapView_->currentMap() ? mapView_->currentMap()->author : QString();
    const QString dir = categoryDir(root, dlg.category());
    const QString key = writeImportedPart(result, name, dir, author, false, &err, freeImportKey(name, dir));
    if (key.isEmpty()) {
        QMessageBox::warning(this, result.kindLabel, tr("Could not save the custom part: %1").arg(err));
        return;
    }
    const QString libKey = registerImportedPart(QDir(dir).filePath(key + QStringLiteral(".xml")));
    if (!libKey.isEmpty()) mapView_->addPartAtViewCenter(libKey);
    statusBar()->showMessage(
        tr("Imported %1 as '%2' (%3 × %4 studs, %n connection point(s))", nullptr,
           result.connections.size())
            .arg(QFileInfo(path).fileName(), key)
            .arg(result.widthStuds).arg(result.heightStuds), 8000);
    offerToSendImportedPart(key);
}

void MainWindow::offerToSendImportedPart(const QString& key) {
    if (!notices_) return;
    notices_->showNotice(QStringLiteral("imported-part"), tr("Part imported"),
        tr("“%1” is in your parts library and on the map. Send it to your server so your club can use it too?").arg(key),
        { NoticeAction{ tr("Send to server…"), [this, key] {
              if (readyToSend()) uploadPartsTo(serverLibrary_->server(), serverLibrary_->token(), false, { key });
          }, true },
          NoticeAction{ tr("Not now"), {} } });
}

void MainWindow::offerToUpdateServerParts(const QStringList& keys) {
    if (keys.isEmpty() || !notices_) return;
    struct Where {
        QUrl server;
        QString token;
        QString label;
    };
    // The server you're signed in to, and the live layout's if it's another.
    QList<Where> servers;
    if (serverLibrary_ && serverLibrary_->state() == ServerLibrary::State::Ready)
        servers.push_back({ serverLibrary_->server(), serverLibrary_->token(), serverLibrary_->label() });
    if (live_ && live_->active() && !liveToken_.isEmpty() && (servers.isEmpty() || servers.front().server != liveServer_))
        servers.push_back({ liveServer_, liveToken_, liveServerName() });
    for (const Where& w : std::as_const(servers)) {
        auto* api = new sync::LibraryApi(this);
        api->setBase(w.server);
        api->setToken(w.token);
        api->customParts([this, api, w, keys](const QList<sync::ServerPart>& onServer) {
            api->deleteLater();
            // Its copies there that you may change: part number → the server's part.
            QList<sync::ServerPart> mine;
            for (const sync::ServerPart& p : onServer)
                if ((p.role == QLatin1String("owner") || p.role == QLatin1String("editor"))
                    && keys.contains(p.partNumber, Qt::CaseInsensitive))
                    mine << p;
            if (mine.isEmpty()) return;
            const QString text = mine.size() == 1
                ? tr("“%1” is on %2 too. Update it there so everyone sees the new version?").arg(mine.front().partNumber, w.label)
                : tr("%n of these parts are on %1 too. Update them there so everyone sees the new versions?", nullptr,
                     static_cast<int>(mine.size())).arg(w.label);
            const QString id = QStringLiteral("updated-part:") + w.server.toString();
            notices_->showNotice(id, tr("Part changed"), text,
                { NoticeAction{ tr("Update on the server"), [this, w, mine] {
                      const QString root = importedPartsRoot();
                      const auto local = root.isEmpty() ? QList<sync::LocalPart>() : sync::PartsUpload::scanFolder(root);
                      auto* send = new sync::LibraryApi(this);
                      send->setBase(w.server);
                      send->setToken(w.token);
                      auto left = std::make_shared<int>(0);
                      auto updated = std::make_shared<int>(0);
                      for (const sync::ServerPart& p : mine) {
                          const auto it = std::find_if(local.cbegin(), local.cend(), [&](const sync::LocalPart& l) {
                              return l.key.compare(p.partNumber, Qt::CaseInsensitive) == 0;
                          });
                          if (it == local.cend()) continue;
                          QJsonObject body = sync::PartsUpload::uploadBody(*it);
                          body.insert(QStringLiteral("partNumber"), p.partNumber);  // as the server spells it
                          ++*left;
                          const auto finished = [this, send, left, updated, w] {
                              if (--*left > 0) return;
                              send->deleteLater();
                              if (*updated > 0)
                                  statusBar()->showMessage(tr("Updated %n part(s) on %1", nullptr, *updated).arg(w.label), 5000);
                              if (live_->active() && w.server == liveServer_) loadLivePartsCatalog();
                          };
                          send->replaceCustomPart(p.id, body, [updated, finished] {
                              ++*updated;
                              finished();
                          }, [this, finished, key = p.partNumber, w](const sync::ServerRefusal& r) {
                              QMessageBox::warning(this, tr("Update on the server"),
                                  tr("Couldn't update “%1” on %2: %3").arg(key, w.label, ServerLibrary::refusalText(r)));
                              finished();
                          });
                      }
                      if (*left == 0) send->deleteLater();
                  }, true },
                  NoticeAction{ tr("Not now"), {} } });
        }, [api](const sync::ServerRefusal&) { api->deleteLater(); });
    }
}

bool MainWindow::reimportPart(const QString& key, bool interactive) {
    const auto meta = parts_.metadata(key);
    if (!meta || !meta->importSource) return false;
    const QString path = meta->importSource->path;
    if (QFileInfo::exists(path)) return reimportFrom(key, path, interactive);
    if (!interactive) return false;
    // Moved or deleted: find it, rather than a dead end.
    QMessageBox box(QMessageBox::Question, tr("Re-import"),
                    tr("The original file for “%1” has moved or been deleted.").arg(shownName(*meta)),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("It was %1. Choose where it is now, and the part is made again from it.")
                               .arg(QDir::toNativeSeparators(path)));
    auto* choose = box.addButton(tr("Choose the file…"), QMessageBox::AcceptRole);
    choose->setObjectName(QStringLiteral("chooseFile"));
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(choose);
    help::addToMessageBox(&box, QStringLiteral("import.moved"));
    box.exec();
    if (box.clickedButton() != choose) return false;
    return replaceImportedPart(key);
}

bool MainWindow::replaceImportedPart(const QString& key) {
    const auto meta = parts_.metadata(key);
    if (!meta || !meta->importSource) return false;
    // The picker starts where the model was, when that folder is still there.
    QString dir = QFileInfo(meta->importSource->path).absolutePath();
    if (!QDir(dir).exists()) dir.clear();
    const QString title = tr("Choose a model for “%1”").arg(shownName(*meta));
    const QString path = askModelFile ? askModelFile(title, dir)
                                      : QFileDialog::getOpenFileName(this, title, dir, modelFileFilter());
    if (path.isEmpty()) return false;
    return reimportFrom(key, path, true);
}

bool MainWindow::reimportFrom(const QString& key, const QString& path, bool interactive) {
    const auto meta = parts_.metadata(key);
    if (!meta || !meta->importSource) return false;
    const auto& src = *meta->importSource;
    const bool sameFile = QFileInfo(path).absoluteFilePath() == QFileInfo(src.path).absoluteFilePath();
    const HeavyRunner runHeavy = [this](const QString& label, const std::function<void(CancelToken&)>& work) {
        return runBackground(this, label, work);
    };
    PreparedPart part = prepareImport(path, currentImportSettings(), parts_, runHeavy);
    if (!part.ok()) {
        if (interactive && !part.cancelled) QMessageBox::warning(this, part.kindLabel, part.error);
        return false;
    }
    // The credit typed last time, unless the model now names its builder.
    if (part.designer.isEmpty()) {
        part.designer = meta->designer;
        part.designerUrl = meta->designerUrl;
    }
    // Last time's turn, and the connection points removed then where the
    // new model still has them.
    applyImportEdits(part, src.quarterTurns, QVector<QPointF>(src.droppedConnections.cbegin(), src.droppedConnections.cend()));
    // The stud alignment and nudge chosen last time, so the part doesn't shift.
    const ImportAlign align = src.align == QLatin1String("bottom") ? ImportAlign::BottomLayer
                              : src.align == QLatin1String("box")  ? ImportAlign::BoundingBox
                                                                   : ImportAlign::Automatic;

    // Same name, same folder, replacing the part.
    const QFileInfo xml(meta->xmlFilePath);
    const QString name = xml.completeBaseName();
    const QString dir = xml.absolutePath();
    if (interactive) {
        const QString root = importedPartsRoot();
        const QString category = QDir(dir).dirName();
        ImportPreviewDialog dlg(std::move(part), importCategories(root), category, {}, this);
        dlg.presetForReimport(name, category);
        dlg.presetAlignment(align, src.nudgeStuds);
        if (!sameFile) dlg.setWindowTitle(tr("Replace %1 with %2").arg(name, QFileInfo(path).fileName()));
        if (dlg.exec() != QDialog::Accepted) return false;
        part = dlg.result();
    } else {
        part = alignPart(part, align, src.nudgeStuds);
    }
    QString err;
    const QString written = writeOverImportedPart(part, key, shownName(*meta), &err);
    if (written.isEmpty()) {
        if (interactive) QMessageBox::warning(this, part.kindLabel, tr("Could not save the custom part: %1").arg(err));
        return false;
    }
    statusBar()->showMessage(sameFile ? tr("Re-imported %1 from %2").arg(written, QFileInfo(path).fileName())
                                      : tr("Replaced %1 with %2").arg(written, QFileInfo(path).fileName()),
                             5000);
    if (interactive) offerToUpdateServerParts({ written });
    return true;
}

void MainWindow::onReimportChangedParts() {
    QStringList changed, moved;
    for (const QString& key : parts_.keys()) {
        const auto meta = parts_.metadata(key);
        if (!meta || !meta->importSource) continue;
        const QFileInfo fi(meta->importSource->path);
        if (!fi.exists())
            moved << key;
        else if (!meta->importSource->modified.isValid() || fi.lastModified() > meta->importSource->modified.addSecs(1))
            changed << key;
    }
    if (changed.isEmpty() && moved.isEmpty()) {
        QMessageBox::information(this, tr("Re-import Changed Parts"),
            tr("No imported part's original file has changed since it was imported."));
        return;
    }
    if (!changed.isEmpty()) {
        changed.sort();
        QMessageBox ask(QMessageBox::Question, tr("Re-import Changed Parts"),
                        tr("The original files of %n imported part(s) changed. Re-import them now?", nullptr,
                           changed.size()),
                        QMessageBox::Yes | QMessageBox::No, this);
        ask.setDetailedText(changed.join(QLatin1Char('\n')));
        if (ask.exec() == QMessageBox::Yes) {
            QStringList done, failed;
            for (const QString& key : std::as_const(changed)) (reimportPart(key, false) ? done : failed) << key;
            if (failed.isEmpty())
                statusBar()->showMessage(tr("Re-imported %n part(s).", nullptr, changed.size()), 5000);
            else
                QMessageBox::warning(this, tr("Re-import Changed Parts"),
                    tr("These parts could not be re-imported:\n%1").arg(failed.join(QLatin1Char('\n'))));
            offerToUpdateServerParts(done);
        }
    }
    if (!moved.isEmpty()) {
        moved.sort();
        findMovedOriginals(moved);
    }
}

void MainWindow::findMovedOriginals(const QStringList& keys) {
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("movedOriginals"));
    dlg.setWindowTitle(tr("Original files moved"));
    auto* layout = new QVBoxLayout(&dlg);
    auto* intro = new QLabel(tr("The original files of these parts have moved or been deleted. Choose where "
                                "each one is now to re-import it, or skip them."), &dlg);
    intro->setWordWrap(true);
    layout->addWidget(help::withHelp(intro, QStringLiteral("import.moved"), &dlg));
    auto* grid = new QGridLayout;
    layout->addLayout(grid);
    auto left = std::make_shared<int>(static_cast<int>(keys.size()));
    for (int row = 0; row < keys.size(); ++row) {
        const QString& key = keys[row];
        const auto meta = parts_.metadata(key);
        auto* name = new QLabel(meta ? shownName(*meta) : key, &dlg);
        if (meta && meta->importSource)
            name->setToolTip(tr("It was %1").arg(QDir::toNativeSeparators(meta->importSource->path)));
        grid->addWidget(name, row, 0);
        auto* choose = new QPushButton(tr("Choose the file…"), &dlg);
        choose->setObjectName(QStringLiteral("choose:") + key);
        choose->setAutoDefault(false);
        grid->addWidget(choose, row, 1);
        connect(choose, &QPushButton::clicked, &dlg, [this, &dlg, choose, key, left] {
            if (!replaceImportedPart(key)) return;
            choose->setEnabled(false);
            choose->setText(tr("Re-imported"));
            if (--*left == 0) dlg.accept();
        });
    }
    auto* buttons = new QDialogButtonBox(&dlg);
    auto* skip = buttons->addButton(tr("Skip"), QDialogButtonBox::RejectRole);
    skip->setObjectName(QStringLiteral("skip"));
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    dlg.exec();
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
    form->addRow(new QLabel(tr("Import %n file(s) as custom parts.", nullptr, files.size()), &opts));
    auto* category = new QComboBox(&opts);
    category->setEditable(true);
    category->addItems(importCategories(root));
    category->setCurrentText(QSettings().value(kLastCategoryKey, QDir(root).dirName()).toString());
    form->addRow(tr("Category:"), category);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &opts);
    connect(bb, &QDialogButtonBox::accepted, &opts, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &opts, &QDialog::reject);
    form->addRow(bb);
    if (opts.exec() != QDialog::Accepted || category->currentText().trimmed().isEmpty()) return;
    const QString cat = category->currentText().trimmed();
    QSettings().setValue(kLastCategoryKey, cat);
    const QString dir = categoryDir(root, cat);

    // Names already taken are asked about one by one, or once for all of them.
    int clashesLeft = 0;
    for (const QString& f : files) clashesLeft += importClashWith(QFileInfo(f).fileName()).isEmpty() ? 0 : 1;
    std::optional<bool> replaceAll;  // set: "do the same for the others"

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

    QStringList done, failed, replaced;
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
        QString key;
        bool over = false;
        if (const QString clash = importClashWith(name); !clash.isEmpty()) {
            --clashesLeft;
            if (replaceAll) {
                over = *replaceAll;
            } else {
                bool same = false;
                const auto choice = askImportClash(name, clash, clashesLeft, &same);
                if (!choice) {
                    token.cancel();
                    break;
                }
                over = !choice->isEmpty();
                if (same) replaceAll = over;
            }
            if (over) key = writeOverImportedPart(part, clash, name, &err);
        }
        if (!over) {
            key = writeImportedPart(part, name, dir, author, false, &err, freeImportKey(name, dir));
            if (!key.isEmpty()) registerImportedPart(QDir(dir).filePath(key + QStringLiteral(".xml")));
        }
        if (key.isEmpty()) {
            failed << tr("%1: %2").arg(name, err);
            continue;
        }
        if (over) replaced << key;
        done << (over ? tr("%1 → %2 (replaced)").arg(name, key)
                      : tr("%1 → %2 (%3 × %4 studs, %n connection point(s))", nullptr, part.connections.size())
                            .arg(name, key).arg(part.widthStuds).arg(part.heightStuds));
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
    offerToUpdateServerParts(replaced);
}

}  // namespace bld::ui
