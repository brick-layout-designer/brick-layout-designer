#pragma once

#include <QDialog>

namespace bld::ui {

// Preferences dialog — tabbed editor mirroring upstream's
// PreferencesForm.cs. Persists every value through QSettings so reopening
// the app restores them. Each tab groups related settings:
//   General    — language, default file paths, splash, zoom speed, undo depth
//   Edition    — default snap step, default rotation step, default paint color
//   Appearance — background color, show grid, show layer panel, selection tint
//   Library    — vendored submodule path (read-only) + user library paths editor
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);
};

}
