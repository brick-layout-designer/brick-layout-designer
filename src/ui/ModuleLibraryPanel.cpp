#include "ModuleLibraryPanel.h"
#include "TouchMode.h"

#include <cmath>

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QScroller>
#include <QTouchEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
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
    auto* host = new QWidget(this);
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
    col->addWidget(list_);

    setWidget(host);

    // Load persisted folder (or default) and populate.
    QSettings s;
    const QString stored = s.value(QString::fromLatin1(kSettingsKey)).toString();
    path_ = stored.isEmpty() ? defaultModuleLibraryPath() : stored;
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
        menu.exec(list_->mapToGlobal(pos));
    });
}

QString ModuleLibraryPanel::libraryPath() const { return path_; }

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

void ModuleLibraryPanel::refresh() {
    list_->clear();
    if (path_.isEmpty() || !QDir(path_).exists()) {
        header_->setText(tr("No folder set. Click \"Folder…\" to choose one."));
        return;
    }
    header_->setText(path_);
    QDir d(path_);
    const QStringList files = d.entryList({ QStringLiteral("*.bbm") },
                                             QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        auto* item = new QListWidgetItem(QFileInfo(f).completeBaseName());
        item->setData(Qt::UserRole, d.absoluteFilePath(f));
        item->setToolTip(d.absoluteFilePath(f));
        list_->addItem(item);
    }
    if (files.isEmpty()) {
        auto* e = new QListWidgetItem(tr("(no modules in this folder)"));
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
