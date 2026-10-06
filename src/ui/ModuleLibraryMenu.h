#pragma once

// The library entries of a placed module's menus (the map's right-click
// menu, the Modules panel and the touch bar's Module button show the
// same, as the web's moduleLibraryMenu.ts): Save to Module library…, Update
// library version… and Update from Module library. MainWindow knows the Module library;
// the menus ask it through a ModuleLibraryInfoFn.

#include <QList>
#include <QString>

#include <functional>

namespace bld::core { struct Module; }

namespace bld::ui {

struct ModuleLibraryEntry {
    QString action;  // "save", "publish" or "pull"
    QString label;
    bool enabled = true;
    QString tip;     // why it's greyed out, or more about it
};

struct ModuleLibraryInfo {
    QString note;  // "in the Module library v3 · v5 is newer"; empty when not linked
    QList<ModuleLibraryEntry> entries;
};

using ModuleLibraryInfoFn = std::function<ModuleLibraryInfo(const core::Module&)>;

}  // namespace bld::ui
