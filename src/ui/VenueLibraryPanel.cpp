#include "VenueLibraryPanel.h"

#include "../saveload/VenueIO.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

namespace bld::ui {

namespace {

constexpr const char* kSettingsKey = "venue/libraryPath";

QString defaultVenueLibraryPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/venues");
}

QString sanitize(QString n) {
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    n.replace(bad, QStringLiteral("_"));
    while (n.startsWith(QLatin1Char('.')) || n.startsWith(QLatin1Char(' '))) n.remove(0, 1);
    while (n.endsWith(QLatin1Char('.'))  || n.endsWith(QLatin1Char(' ')))  n.chop(1);
    if (n.isEmpty()) n = QStringLiteral("Venue");
    return n;
}

QString detailText(const bld::core::Venue& v) {
    const int walls  = static_cast<int>(std::count_if(v.edges.begin(), v.edges.end(),
        [](const auto& e){ return e.kind == bld::core::EdgeKind::Wall; }));
    const int doors  = static_cast<int>(std::count_if(v.edges.begin(), v.edges.end(),
        [](const auto& e){ return e.kind == bld::core::EdgeKind::Door; }));
    const int obs    = v.obstacles.size();
    const double mW  = v.minWalkwayStuds * 0.8;  // ~mm (8 studs/10mm)
    return QStringLiteral("%1\n%2 wall seg · %3 door · %4 obstacle · walkway ≥ %5 mm")
        .arg(v.name.isEmpty() ? QObject::tr("(unnamed)") : v.name)
        .arg(walls).arg(doors).arg(obs)
        .arg(static_cast<int>(mW));
}

}  // namespace

VenueLibraryPanel::VenueLibraryPanel(QWidget* parent)
    : QDockWidget(tr("Venue Library"), parent) {
    auto* host = new QWidget(this);
    auto* col  = new QVBoxLayout(host);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);

    // Header row: path label + Folder… button
    auto* headerRow = new QHBoxLayout();
    pathLabel_ = new QLabel(host);
    pathLabel_->setWordWrap(true);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* folderBtn = new QPushButton(tr("Folder…"), host);
    folderBtn->setToolTip(tr("Choose the folder to scan for venue .bld-venue files"));
    auto* refreshBtn = new QPushButton(tr("Refresh"), host);
    headerRow->addWidget(pathLabel_, 1);
    headerRow->addWidget(folderBtn);
    headerRow->addWidget(refreshBtn);
    col->addLayout(headerRow);

    // Venue list
    list_ = new QListWidget(host);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    col->addWidget(list_, 1);

    // Detail area (name, stats)
    detailLabel_ = new QLabel(host);
    detailLabel_->setWordWrap(true);
    detailLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailLabel_->setMinimumHeight(50);
    detailLabel_->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
    col->addWidget(detailLabel_);

    // Action buttons
    auto* btnRow = new QHBoxLayout();
    loadBtn_   = new QPushButton(tr("Load into Project"), host);
    startBtn_  = new QPushButton(tr("Start Layout"), host);
    startBtn_->setObjectName(QStringLiteral("venueStartLayout"));
    startBtn_->setToolTip(tr("Start a new layout with this venue"));
    saveBtn_   = new QPushButton(tr("Save Current Venue"), host);
    deleteBtn_ = new QPushButton(tr("Delete"), host);
    renameBtn_ = new QPushButton(tr("Rename…"), host);
    btnRow->addWidget(loadBtn_);
    btnRow->addWidget(startBtn_);
    btnRow->addWidget(saveBtn_);
    btnRow->addWidget(renameBtn_);
    btnRow->addWidget(deleteBtn_);
    col->addLayout(btnRow);

    setWidget(host);

    QSettings s;
    const QString stored = s.value(QString::fromLatin1(kSettingsKey)).toString();
    path_ = stored.isEmpty() ? defaultVenueLibraryPath() : stored;
    refresh();

    connect(folderBtn,  &QPushButton::clicked, this, &VenueLibraryPanel::onChooseFolder);
    connect(refreshBtn, &QPushButton::clicked, this, &VenueLibraryPanel::refresh);
    connect(loadBtn_,   &QPushButton::clicked, this, &VenueLibraryPanel::onLoad);
    connect(startBtn_,  &QPushButton::clicked, this, &VenueLibraryPanel::onStartLayout);
    connect(saveBtn_,   &QPushButton::clicked, this, [this]{ emit venueSaveRequested(); });
    connect(deleteBtn_, &QPushButton::clicked, this, &VenueLibraryPanel::onDelete);
    connect(renameBtn_, &QPushButton::clicked, this, &VenueLibraryPanel::onRename);
    connect(list_, &QListWidget::itemSelectionChanged, this, &VenueLibraryPanel::onSelectionChanged);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*){ onLoad(); });
}

QString VenueLibraryPanel::libraryPath() const { return path_; }

void VenueLibraryPanel::setLibraryPath(const QString& dir) {
    path_ = dir;
    QSettings().setValue(QString::fromLatin1(kSettingsKey), path_);
    refresh();
}

void VenueLibraryPanel::refresh() {
    const QString sel = selectedPath();
    list_->clear();
    detailLabel_->clear();

    if (path_.isEmpty() || !QDir(path_).exists()) {
        pathLabel_->setText(tr("No folder set. Click \"Folder…\" to choose one."));
        updateButtons();
        return;
    }

    QDir().mkpath(path_);
    pathLabel_->setText(path_);

    QDir d(path_);
    const QStringList files = d.entryList({ QStringLiteral("*.bld-venue") },
                                            QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        const QString absPath = d.absoluteFilePath(f);
        auto* item = new QListWidgetItem(QFileInfo(f).completeBaseName());
        item->setData(Qt::UserRole, absPath);
        item->setToolTip(absPath);
        list_->addItem(item);
        if (absPath == sel)
            list_->setCurrentItem(item);
    }

    if (files.isEmpty()) {
        auto* e = new QListWidgetItem(tr("(no saved venues)"));
        e->setFlags(Qt::NoItemFlags);
        list_->addItem(e);
    }

    updateButtons();
}

void VenueLibraryPanel::saveVenue(const std::optional<core::Venue>& venue) {
    if (!venue) {
        QMessageBox::information(this, tr("Save venue"),
            tr("There is no venue on the current project."));
        return;
    }

    QDir().mkpath(path_);

    const QString defName = venue->name.isEmpty() ? tr("Venue") : venue->name;
    bool ok = false;
    const QString raw = QInputDialog::getText(this, tr("Save venue to library"),
        tr("Name for this venue:"), QLineEdit::Normal, defName, &ok);
    if (!ok || raw.trimmed().isEmpty()) return;

    const QString filename = sanitize(raw.trimmed()) + QStringLiteral(".bld-venue");
    const QString target   = QDir(path_).filePath(filename);

    if (QFile::exists(target)) {
        const auto btn = QMessageBox::question(this, tr("Save venue"),
            tr("%1 already exists. Overwrite?").arg(QFileInfo(target).completeBaseName()));
        if (btn != QMessageBox::Yes) return;
    }

    QString err;
    if (!saveload::writeVenueFile(target, *venue, &err)) {
        QMessageBox::warning(this, tr("Save venue"),
            tr("Could not write file: %1").arg(err));
        return;
    }

    refresh();

    // Re-select the just-saved item.
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString() == target) {
            list_->setCurrentRow(i);
            break;
        }
    }
}

void VenueLibraryPanel::onChooseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Venue library folder"), path_,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) setLibraryPath(dir);
}

std::optional<core::Venue> VenueLibraryPanel::readSelected(const QString& title) {
    const QString p = selectedPath();
    if (p.isEmpty()) return std::nullopt;
    QString err;
    auto v = saveload::readVenueFile(p, &err);
    if (!v) {
        QMessageBox::warning(this, title,
            tr("Could not read %1: %2").arg(QFileInfo(p).fileName(), err));
    }
    return v;
}

void VenueLibraryPanel::onLoad() {
    if (auto v = readSelected(tr("Load venue"))) emit venueLoadRequested(*v);
}

void VenueLibraryPanel::onStartLayout() {
    if (auto v = readSelected(tr("Start layout"))) emit newLayoutRequested(*v);
}

void VenueLibraryPanel::onDelete() {
    const QString p = selectedPath();
    if (p.isEmpty()) return;
    const QString name = QFileInfo(p).completeBaseName();
    const auto btn = QMessageBox::question(this, tr("Delete venue"),
        tr("Delete \"%1\" from the library?").arg(name));
    if (btn != QMessageBox::Yes) return;
    if (!QFile::remove(p)) {
        QMessageBox::warning(this, tr("Delete venue"),
            tr("Could not delete %1.").arg(name));
        return;
    }
    refresh();
}

void VenueLibraryPanel::onRename() {
    const QString p = selectedPath();
    if (p.isEmpty()) return;
    const QString oldName = QFileInfo(p).completeBaseName();
    bool ok = false;
    const QString raw = QInputDialog::getText(this, tr("Rename venue"),
        tr("New name:"), QLineEdit::Normal, oldName, &ok);
    if (!ok || raw.trimmed().isEmpty() || sanitize(raw.trimmed()) == oldName) return;
    const QString newPath = QDir(QFileInfo(p).dir()).filePath(
        sanitize(raw.trimmed()) + QStringLiteral(".bld-venue"));
    if (QFile::exists(newPath)) {
        QMessageBox::warning(this, tr("Rename venue"),
            tr("\"%1\" already exists.").arg(sanitize(raw.trimmed())));
        return;
    }
    if (!QFile::rename(p, newPath)) {
        QMessageBox::warning(this, tr("Rename venue"), tr("Could not rename the file."));
        return;
    }
    refresh();
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString() == newPath) {
            list_->setCurrentRow(i);
            break;
        }
    }
}

void VenueLibraryPanel::onSelectionChanged() {
    const QString p = selectedPath();
    if (p.isEmpty()) {
        detailLabel_->clear();
    } else {
        QString err;
        const auto v = saveload::readVenueFile(p, &err);
        detailLabel_->setText(v ? detailText(*v) : tr("(could not read file)"));
    }
    updateButtons();
}

void VenueLibraryPanel::updateButtons() {
    const bool has = !selectedPath().isEmpty();
    loadBtn_->setEnabled(has);
    startBtn_->setEnabled(has);
    deleteBtn_->setEnabled(has);
    renameBtn_->setEnabled(has);
}

QString VenueLibraryPanel::selectedPath() const {
    const auto* item = list_->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString();
}

}  // namespace bld::ui
