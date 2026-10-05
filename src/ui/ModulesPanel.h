#pragma once

#include <QDockWidget>

class QListWidget;
class QToolButton;

namespace bld::core { class Map; }

namespace bld::ui {

// List of modules (fork-only cross-layer groups). Right-click invokes Delete.
// moduleDeleteRequested is emitted with the module id so MainWindow can push
// an undoable DeleteModuleCommand.
class ModulesPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit ModulesPanel(QWidget* parent = nullptr);
    void setMap(const core::Map* map);

signals:
    void moduleDeleteRequested(const QString& moduleId);
    void createModuleRequested();
    void importBbmRequested();
    void selectMembersRequested(const QString& moduleId);
    void flattenRequested(const QString& moduleId);
    void rescanRequested(const QString& moduleId);
    void moveRequested(const QString& moduleId, double dxStuds, double dyStuds);
    void rotateRequested(const QString& moduleId, double degrees);
    void saveToLibraryRequested(const QString& moduleId);
    void cloneRequested(const QString& moduleId);
    void renameRequested(const QString& moduleId);
    // The module's look: its colours (the Module look dialog), and its name on or off.
    void lookRequested(const QString& moduleId);
    void showNameRequested(const QString& moduleId, bool show);

public:
    // The ⋯ button: the current module's menu.
    QToolButton* moreButton() const { return more_; }

private:
    void showMenu(const QString& id, const QPoint& globalPos);
    QListWidget* list_ = nullptr;
    QToolButton* more_ = nullptr;
    const core::Map* map_ = nullptr;
};

}
