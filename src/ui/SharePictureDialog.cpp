#include "SharePictureDialog.h"

#include "help/HelpButton.h"

#include "../core/Map.h"

#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace bld::ui {

namespace {

const QString kFolderKey = QStringLiteral("views/exportFolder");
const QString kScaleKey = QStringLiteral("views/exportScale");
const QString kPictureFolderKey = QStringLiteral("views/pictureFolder");

QString picturesFolder() {
    const QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return pics.isEmpty() ? QDir::homePath() : pics;
}

QString exportMessage(const views::ExportAllResult& r, const QString& folder) {
    const QString where = QDir::toNativeSeparators(folder);
    QString msg = r.files.isEmpty()      ? QObject::tr("There is nothing to show yet.")
                  : r.files.size() == 1 ? QObject::tr("Saved 1 picture in “%1”.").arg(where)
                                         : QObject::tr("Saved %1 pictures in “%2”.").arg(r.files.size()).arg(where);
    if (!r.skipped.isEmpty())
        msg += QLatin1Char(' ') + QObject::tr("Nothing to show in: %1.").arg(r.skipped.join(QStringLiteral(", ")));
    if (!r.failed.isEmpty())
        msg += QLatin1Char(' ') + QObject::tr("Could not write: %1.").arg(r.failed.join(QStringLiteral(", ")));
    return msg;
}

}  // namespace

ExportViewsPrefs loadExportViewsPrefs() {
    QSettings s;
    ExportViewsPrefs p;
    p.folder = s.value(kFolderKey).toString();
    const double scale = s.value(kScaleKey, 2.0).toDouble();
    p.scale = scale > 0 && scale <= 8 ? scale : 2.0;
    return p;
}

void saveExportViewsPrefs(const ExportViewsPrefs& p) {
    QSettings s;
    s.setValue(kFolderKey, p.folder);
    s.setValue(kScaleKey, p.scale);
}

QString runExportAllViews(QWidget* parent, const core::Map& map, parts::PartsLibrary& parts,
                          const QString& layoutTitle, bool chooseFolder) {
    ExportViewsPrefs prefs = loadExportViewsPrefs();
    if (chooseFolder || prefs.folder.isEmpty() || !QDir(prefs.folder).exists()) {
        const QString dir = QFileDialog::getExistingDirectory(
            parent, QObject::tr("Choose a folder for the pictures"),
            prefs.folder.isEmpty() ? picturesFolder() : prefs.folder);
        if (dir.isEmpty()) return {};
        prefs.folder = dir;
    }
    const auto r = views::exportAllViews(map, parts, layoutTitle, prefs.scale, prefs.folder);
    saveExportViewsPrefs(prefs);
    return exportMessage(r, prefs.folder);
}

SharePictureDialog::SharePictureDialog(Input input, QWidget* parent) : QDialog(parent), in_(std::move(input)) {
    setWindowTitle(tr("Share Picture"));
    setObjectName(QStringLiteral("SharePictureDialog"));
    if (in_.map && in_.parts) renderer_ = std::make_unique<views::PictureRenderer>(*in_.map, *in_.parts);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(20, 18, 20, 16);
    col->setSpacing(10);
    col->addWidget(help::headingWithHelp(tr("Share a picture"), QStringLiteral("share.picture"), this));

    auto* pickRow = new QHBoxLayout();
    auto* pickLabel = new QLabel(tr("Picture of:"), this);
    choiceBox_ = new QComboBox(this);
    choiceBox_->setObjectName(QStringLiteral("pictureOf"));
    pickLabel->setBuddy(choiceBox_);
    choiceBox_->addItem(tr("Whole layout"), views::wholeLayout().id);
    if (in_.screen) choiceBox_->addItem(tr("What’s on screen"), screenChoice());
    if (in_.map)
        for (const auto& v : in_.map->sidecar.views) choiceBox_->addItem(v.name.isEmpty() ? tr("View") : v.name, v.id);
    pickRow->addWidget(pickLabel);
    pickRow->addWidget(choiceBox_, 1);
    col->addLayout(pickRow);

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("picturePreview"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFixedSize(480, 300);
    preview_->setFrameShape(QFrame::StyledPanel);
    preview_->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(preview_, 0, Qt::AlignHCenter);
    size_ = new QLabel(this);
    size_->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(size_);

    auto* actRow = new QHBoxLayout();
    saveBtn_ = new QPushButton(tr("Save as PNG…"), this);
    saveBtn_->setObjectName(QStringLiteral("pictureSave"));
    saveBtn_->setProperty("accent", true);
    copyBtn_ = new QPushButton(tr("Copy picture"), this);
    copyBtn_->setObjectName(QStringLiteral("pictureCopy"));
    copyBtn_->setToolTip(tr("Paste it into a message or a document"));
    actRow->addWidget(saveBtn_, 1);
    actRow->addWidget(copyBtn_, 1);
    col->addLayout(actRow);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("pictureStatus"));
    status_->setWordWrap(true);
    status_->setMinimumHeight(status_->fontMetrics().height());
    col->addWidget(status_);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setForegroundRole(QPalette::Mid);
    col->addWidget(line);
    col->addWidget(help::headingWithHelp(tr("Export all views"), QStringLiteral("share.exportAllViews"), this));
    const int n = in_.map ? static_cast<int>(in_.map->sidecar.views.size()) : 0;
    auto* about = new QLabel(
        n == 0   ? tr("No saved views yet, so this makes one picture of the whole layout.")
        : n == 1 ? tr("A picture of your saved view, in a folder. Do it again after a change to get a new "
                      "picture with the same name.")
                 : tr("One picture of each of your %1 saved views, in a folder. Do it again after a change to "
                      "get new pictures with the same names.")
                       .arg(n),
        this);
    about->setWordWrap(true);
    about->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(about);
    auto* exportRow = new QHBoxLayout();
    auto* seg = new QFrame(this);
    seg->setObjectName(QStringLiteral("Segmented"));
    seg->setAccessibleName(tr("Picture size"));
    auto* segRow = new QHBoxLayout(seg);
    segRow->setContentsMargins(3, 3, 3, 3);
    segRow->setSpacing(2);
    scaleGroup_ = new QButtonGroup(this);
    scaleGroup_->setExclusive(true);
    const auto sizes = views::exportSizes();
    for (int i = 0; i < static_cast<int>(sizes.size()); ++i) {
        auto* b = new QPushButton(sizes[i].label, seg);
        b->setObjectName(QStringLiteral("size:") + sizes[i].label);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        scaleGroup_->addButton(b, i);
        segRow->addWidget(b);
    }
    setExportScale(loadExportViewsPrefs().scale);
    auto* exportBtn = new QPushButton(n > 0 ? tr("Export all views") : tr("Export picture"), this);
    exportBtn->setObjectName(QStringLiteral("exportAllViews"));
    exportRow->addWidget(seg);
    exportRow->addStretch(1);
    exportRow->addWidget(exportBtn);
    col->addLayout(exportRow);
    auto* folderRow = new QHBoxLayout();
    folder_ = new QLabel(this);
    folder_->setObjectName(QStringLiteral("exportFolder"));
    folder_->setForegroundRole(QPalette::PlaceholderText);
    folder_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* changeBtn = new QPushButton(tr("Change folder…"), this);
    changeBtn->setObjectName(QStringLiteral("exportChangeFolder"));
    changeBtn->setFlat(true);
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(changeBtn);
    col->addLayout(folderRow);
    refreshFolder();

    auto* line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setForegroundRole(QPalette::Mid);
    col->addWidget(line2);
    auto* bottom = new QHBoxLayout();
    auto* more = new QPushButton(tr("More options…"), this);
    more->setObjectName(QStringLiteral("pictureMoreOptions"));
    more->setFlat(true);
    more->setToolTip(tr("Pick the exact size, a see-through background or a watermark"));
    auto* done = new QPushButton(tr("Done"), this);
    done->setObjectName(QStringLiteral("pictureDone"));
    bottom->addWidget(more);
    bottom->addStretch(1);
    bottom->addWidget(done);
    col->addLayout(bottom);

    connect(choiceBox_, &QComboBox::currentIndexChanged, this, [this](int) { choose(choice()); });
    connect(saveBtn_, &QPushButton::clicked, this, [this] {
        QSettings s;
        const QString dir = s.value(kPictureFolderKey, picturesFolder()).toString();
        const QString path = QFileDialog::getSaveFileName(this, tr("Save picture"), QDir(dir).filePath(fileName()),
                                                          tr("PNG picture (*.png)"));
        if (path.isEmpty()) return;
        if (savePictureTo(path)) s.setValue(kPictureFolderKey, QFileInfo(path).absolutePath());
    });
    connect(copyBtn_, &QPushButton::clicked, this, &SharePictureDialog::copyPicture);
    connect(scaleGroup_, &QButtonGroup::idClicked, this, [this](int) {
        ExportViewsPrefs p = loadExportViewsPrefs();
        p.scale = exportScale();
        saveExportViewsPrefs(p);
    });
    const auto runExport = [this](bool chooseFolder) {
        if (!in_.map || !in_.parts) return;
        ExportViewsPrefs p = loadExportViewsPrefs();
        p.scale = exportScale();
        saveExportViewsPrefs(p);
        const QString msg = runExportAllViews(this, *in_.map, *in_.parts, in_.layoutTitle, chooseFolder);
        if (!msg.isEmpty()) status_->setText(msg);
        refreshFolder();
    };
    connect(exportBtn, &QPushButton::clicked, this, [runExport] { runExport(false); });
    connect(changeBtn, &QPushButton::clicked, this, [runExport] { runExport(true); });
    connect(more, &QPushButton::clicked, this, [this] {
        accept();
        emit moreOptionsRequested();
    });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);

    const int at = choiceBox_->findData(in_.initialChoice.isEmpty() ? views::wholeLayout().id : in_.initialChoice);
    {
        const QSignalBlocker quiet(choiceBox_);
        choiceBox_->setCurrentIndex(std::max(0, at));
    }
    choose(choice());
    choiceBox_->setFocus();  // not the "?" first
}

SharePictureDialog::~SharePictureDialog() = default;

QString SharePictureDialog::choice() const { return choiceBox_->currentData().toString(); }

void SharePictureDialog::choose(const QString& c) {
    const int at = choiceBox_->findData(c);
    if (at >= 0 && at != choiceBox_->currentIndex()) {
        choiceBox_->setCurrentIndex(at);  // comes back here
        return;
    }
    status_->clear();
    picture_ = QImage();
    std::optional<views::PictureSpec> spec;
    if (in_.map) {
        if (c == screenChoice()) {
            spec = in_.screen;
        } else {
            core::SavedView view = views::wholeLayout();
            for (const auto& v : in_.map->sidecar.views)
                if (v.id == c) view = v;
            spec = views::viewPicture(view, *in_.map);
        }
    }
    if (spec && renderer_)
        picture_ = renderer_->render(*spec, views::pictureSize(spec->region, views::shareScale(spec->region)));
    const bool ok = !picture_.isNull();
    saveBtn_->setEnabled(ok);
    copyBtn_->setEnabled(ok);
    if (!ok) {
        preview_->setPixmap(QPixmap());
        preview_->setText(tr("There is nothing to show yet."));
        size_->clear();
        return;
    }
    const QSize box = preview_->contentsRect().size();
    QPixmap pm = QPixmap::fromImage(picture_.scaled(box * devicePixelRatioF(), Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation));
    pm.setDevicePixelRatio(devicePixelRatioF());
    preview_->setPixmap(pm);
    size_->setText(tr("%1 × %2 pixels").arg(picture_.width()).arg(picture_.height()));
}

QString SharePictureDialog::fileName() const {
    return views::pictureFileName(in_.layoutTitle, choiceBox_->currentText());
}

bool SharePictureDialog::savePictureTo(const QString& path) {
    if (picture_.isNull()) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || !picture_.save(&f, "PNG") || !f.commit()) {
        status_->setText(tr("Could not save the picture to “%1”.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    status_->setText(tr("Saved as “%1”.").arg(QFileInfo(path).fileName()));
    return true;
}

void SharePictureDialog::copyPicture() {
    if (picture_.isNull()) return;
    QGuiApplication::clipboard()->setImage(picture_);
    status_->setText(tr("Copied. Paste it into a message or a document."));
}

QString SharePictureDialog::status() const { return status_->text(); }

views::ExportAllResult SharePictureDialog::exportAllTo(const QString& folder) {
    views::ExportAllResult r;
    if (!in_.map || !in_.parts) return r;
    r = views::exportAllViews(*in_.map, *in_.parts, in_.layoutTitle, exportScale(), folder);
    saveExportViewsPrefs({ folder, exportScale() });
    status_->setText(exportMessage(r, folder));
    refreshFolder();
    return r;
}

double SharePictureDialog::exportScale() const {
    const auto sizes = views::exportSizes();
    const int id = scaleGroup_->checkedId();
    return id >= 0 && id < static_cast<int>(sizes.size()) ? sizes[id].scale : 2.0;
}

void SharePictureDialog::setExportScale(double scale) {
    const auto sizes = views::exportSizes();
    int pick = 1;
    for (int i = 0; i < static_cast<int>(sizes.size()); ++i)
        if (qFuzzyCompare(sizes[i].scale, scale)) pick = i;
    if (auto* b = scaleGroup_->button(pick)) b->setChecked(true);
}

void SharePictureDialog::refreshFolder() {
    const QString f = loadExportViewsPrefs().folder;
    folder_->setText(f.isEmpty() ? tr("You pick a folder the first time.")
                                 : tr("Saves to: %1").arg(QDir::toNativeSeparators(f)));
}

}  // namespace bld::ui
