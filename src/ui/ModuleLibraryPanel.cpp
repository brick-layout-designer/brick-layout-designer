#include "ModuleLibraryPanel.h"
#include "TouchMode.h"
#include "ModuleThumbnail.h"
#include "ConfirmDialog.h"
#include "saveload/SidecarIO.h"

#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QScroller>
#include <QIcon>
#include <QPixmap>
#include <QTimer>
#include <QTouchEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace bld::ui {

namespace {
constexpr const char* kSettingsKey = "modules/libraryPath";

QString defaultModuleLibraryPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/modules");
}

// Minimal QListWidget subclass that encodes the dragged item's file path
// under our custom module MIME type. MapView::dropEvent picks it up and
// imports the module at the drop position.
class ModuleListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;
    QStringList mimeTypes() const override {
        return { QString::fromLatin1(kModuleDragMimeType) };
    }
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        auto* m = new QMimeData;
        if (!items.isEmpty()) {
            const QString path = items.first()->data(Qt::UserRole).toString();
            m->setData(QString::fromLatin1(kModuleDragMimeType), path.toUtf8());
        }
        return m;
    }
};
}

ModuleLibraryPanel::ModuleLibraryPanel(QWidget* parent)
    : QDockWidget(tr("Module library"), parent) {
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("moduleLibraryTabs"));
    tabs_->setTabBarAutoHide(true);
    tabs_->setDocumentMode(true);
    // Short names that share the dock's width, never hidden behind scroll arrows.
    tabs_->tabBar()->setExpanding(true);
    tabs_->setElideMode(Qt::ElideRight);
    tabs_->setUsesScrollButtons(false);
    auto* host = new QWidget(tabs_);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(2);

    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    header_ = new QLabel(host);
    header_->setWordWrap(true);
    auto* chooseBtn = new QPushButton(tr("Folder…"), host);
    chooseBtn->setToolTip(tr("Choose the folder to scan for module .bbm files"));
    auto* refreshBtn = new QPushButton(tr("Refresh"), host);
    row->addWidget(header_, 1);
    row->addWidget(chooseBtn);
    row->addWidget(refreshBtn);
    col->addLayout(row);

    list_ = new ModuleListWidget(host);
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    // Touch: drag a module sideways out onto the map; up and down flicks
    // the list (eventFilter), as in the Parts panel.
    list_->setProperty("bldNoTouchScroll", true);
    list_->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    list_->viewport()->installEventFilter(this);
    QScroller::scroller(list_->viewport());
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setIconSize(QSize(48, 48));
    col->addWidget(list_);
    catalogLink_ = new QPushButton(tr("Browse the catalog on the web…"), host);
    catalogLink_->setObjectName(QStringLiteral("browseCatalog"));
    catalogLink_->setFlat(true);
    catalogLink_->setCursor(Qt::PointingHandCursor);
    catalogLink_->setToolTip(tr("Modules people shared for everyone, on your server's website"));
    catalogLink_->setVisible(false);

    connect(catalogLink_, &QPushButton::clicked, this, &ModuleLibraryPanel::browseCatalogRequested);
    col->addWidget(catalogLink_, 0, Qt::AlignLeft);
    // Modules made on the website live on the server, not in this folder.
    webModulesLink_ = new QPushButton(tr("Your modules on the web…"), host);
    webModulesLink_->setObjectName(QStringLiteral("webModules"));
    webModulesLink_->setFlat(true);
    webModulesLink_->setCursor(Qt::PointingHandCursor);
    webModulesLink_->setToolTip(tr("Modules you saved on your server's website; this list shows the ones on this computer"));
    webModulesLink_->setVisible(false);
    connect(webModulesLink_, &QPushButton::clicked, this, &ModuleLibraryPanel::webModulesRequested);
    col->addWidget(webModulesLink_, 0, Qt::AlignLeft);

    tabs_->addTab(host, tr("This computer"));
    tabs_->setTabToolTip(0, tr("Modules saved as files in a folder on this computer"));
    setWidget(tabs_);

    // Load persisted folder (or default) and populate.
    QSettings s;
    const QString stored = s.value(QString::fromLatin1(kSettingsKey)).toString();
    path_ = stored.isEmpty() ? defaultModuleLibraryPath() : stored;
    confirm_ = [this](const QString& name) {
        DeleteWording w;
        w.removes = tr("The module file is deleted from this computer.");
        w.keeps = tr("Layouts that already use it don’t change.");
        return ConfirmDialog::confirmDelete(this, name, w);
    };
    refresh();

    connect(chooseBtn,  &QPushButton::clicked,     this, &ModuleLibraryPanel::onChooseFolder);
    connect(refreshBtn, &QPushButton::clicked,     this, &ModuleLibraryPanel::refresh);
    connect(list_, &QListWidget::itemActivated,    this, &ModuleLibraryPanel::onActivated);
    // Right-click → Import action.
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos){
        auto* it = list_->itemAt(pos);
        if (!it) return;
        QMenu menu(this);
        auto* act = menu.addAction(tr("Import into map"));
        connect(act, &QAction::triggered, [this, it]{ onActivated(it); });
        const QString path = it->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            menu.addSeparator();
            auto* del = menu.addAction(tr("Delete…"));
            del->setObjectName(QStringLiteral("deleteLocalModule"));
            connect(del, &QAction::triggered, this, [this, path] { deleteModule(path); });
        }
        menu.exec(list_->mapToGlobal(pos));
    });
}

QString ModuleLibraryPanel::libraryPath() const { return path_; }

bool ModuleLibraryPanel::deleteModule(const QString& bbmPath) {
    const QString name = QFileInfo(bbmPath).completeBaseName();
    if (!confirm_(name)) return false;
    if (!QFile::remove(bbmPath)) {
        QMessageBox::warning(this, tr("Delete module"), tr("Could not delete %1.").arg(name));
        return false;
    }
    QFile::remove(saveload::sidecarPathFor(bbmPath));
    refresh();
    return true;
}

bool ModuleLibraryPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (list_ && obj == list_->viewport()) {
        switch (ev->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            if (handleTouch(static_cast<QTouchEvent*>(ev))) return true;
            break;
        default:
            break;
        }
    }
    return QDockWidget::eventFilter(obj, ev);
}

bool ModuleLibraryPanel::handleTouch(QTouchEvent* e) {
    if (!TouchMode::fromTouchScreen(e) || e->points().isEmpty()) return false;
    e->accept();
    const QEventPoint& p = e->points().first();
    const QPointF pos = p.position();
    QScroller* scroller = QScroller::scroller(list_->viewport());
    const auto ts = static_cast<qint64>(e->timestamp());
    switch (e->type()) {
    case QEvent::TouchBegin: {
        touch_ = TouchState::Undecided;
        touchStart_ = pos;
        QListWidgetItem* it = list_->itemAt(pos.toPoint());
        touchPath_ = it ? it->data(Qt::UserRole).toString() : QString();
        scroller->stop();
        return true;
    }
    case QEvent::TouchUpdate: {
        const QPointF d = pos - touchStart_;
        if (touch_ == TouchState::Undecided && d.manhattanLength() > 12) {
            // Sideways off a module: take it along; otherwise scroll.
            if (!touchPath_.isEmpty() && std::abs(d.x()) > std::abs(d.y())) {
                touch_ = TouchState::Drag;
            } else {
                touch_ = TouchState::Scroll;
                scroller->handleInput(QScroller::InputPress, touchStart_, ts);
            }
        }
        if (touch_ == TouchState::Drag) emit touchDragMoved(touchPath_, p.globalPosition().toPoint());
        else if (touch_ == TouchState::Scroll) scroller->handleInput(QScroller::InputMove, pos, ts);
        return true;
    }
    case QEvent::TouchEnd:
        if (touch_ == TouchState::Drag) {
            emit touchDragDropped(touchPath_, p.globalPosition().toPoint());
        } else if (touch_ == TouchState::Scroll) {
            scroller->handleInput(QScroller::InputRelease, pos, ts);
        } else if (touch_ == TouchState::Undecided) {
            if (QListWidgetItem* it = list_->itemAt(touchStart_.toPoint())) list_->setCurrentItem(it);
        }
        touch_ = TouchState::None;
        return true;
    default:  // TouchCancel
        if (touch_ == TouchState::Drag) emit touchDragCancelled();
        touch_ = TouchState::None;
        return true;
    }
}

void ModuleLibraryPanel::setLibraryPath(const QString& dir) {
    path_ = dir;
    QSettings().setValue(QString::fromLatin1(kSettingsKey), path_);
    refresh();
}

void ModuleLibraryPanel::setCatalogLinkVisible(bool visible) {
    catalogLink_->setVisible(visible);
    webModulesLink_->setVisible(visible);
}

int ModuleLibraryPanel::addTab(QWidget* page, const QString& label) { return tabs_->addTab(page, label); }

void ModuleLibraryPanel::setParts(parts::PartsLibrary* parts) {
    parts_ = parts;
    refresh();
}

void ModuleLibraryPanel::scheduleThumbnails() {
    if (thumbScheduled_ || pendingThumbs_.isEmpty()) return;
    thumbScheduled_ = true;
    QTimer::singleShot(0, this, &ModuleLibraryPanel::drawNextThumbnail);
}

void ModuleLibraryPanel::drawNextThumbnail() {
    thumbScheduled_ = false;
    if (!parts_ || pendingThumbs_.isEmpty()) return;
    const QString path = pendingThumbs_.takeFirst();
    const QImage img = moduleThumbnail(path, *parts_, thumbCacheDir_);
    if (!img.isNull()) {
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* it = list_->item(i);
            if (it->data(Qt::UserRole).toString() == path) it->setIcon(QIcon(QPixmap::fromImage(img)));
        }
    }
    // One at a time, so a big folder doesn't hold up the window.
    scheduleThumbnails();
}

void ModuleLibraryPanel::refresh() {
    list_->clear();
    pendingThumbs_.clear();
    if (path_.isEmpty() || !QDir(path_).exists()) {
        header_->setText(tr("No folder set. Click \"Folder…\" to choose one."));
        return;
    }
    header_->setText(tr("On this computer: %1").arg(QDir(path_).dirName()));
    header_->setToolTip(path_);
    QDir d(path_);
    const QStringList files = d.entryList({ QStringLiteral("*.bbm") },
                                             QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        auto* item = new QListWidgetItem(QFileInfo(f).completeBaseName());
        item->setData(Qt::UserRole, d.absoluteFilePath(f));
        item->setToolTip(d.absoluteFilePath(f));
        list_->addItem(item);
        if (parts_) pendingThumbs_ << d.absoluteFilePath(f);
    }
    scheduleThumbnails();
    if (files.isEmpty()) {
        auto* e = new QListWidgetItem(tr("No modules here yet. Use Modules › Save Selection as Module… to add one."));
        e->setFlags(Qt::NoItemFlags);
        list_->addItem(e);
    }
}

void ModuleLibraryPanel::onChooseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Module library folder"), path_,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) setLibraryPath(dir);
}

void ModuleLibraryPanel::onActivated(QListWidgetItem* item) {
    if (!item) return;
    const QString p = item->data(Qt::UserRole).toString();
    if (!p.isEmpty()) emit moduleImportRequested(p);
}

}
