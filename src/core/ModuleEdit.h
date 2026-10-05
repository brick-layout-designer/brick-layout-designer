#pragma once

// Modules in a layout act as one piece: a click on any part picks the
// whole module, drags and box-select take it whole, and it moves and turns
// as a unit. "Edit module" opens one module to change it part by part,
// with the rest of the layout out of reach; "Pin in place" keeps a module
// from moving as a whole (Edit module still works). Pure functions over
// the sidecar's modules; the web's apps/web/src/editor/moduleEdit.ts is
// the same.

#include "Module.h"

#include <QCoreApplication>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

namespace bld::core {

// The module each part belongs to (the first one listing it).
inline QHash<QString, const Module*> moduleByPart(const std::vector<Module>& modules) {
    QHash<QString, const Module*> out;
    for (const auto& m : modules)
        for (const auto& id : m.memberIds)
            if (!out.contains(id)) out.insert(id, &m);
    return out;
}

inline const Module* findModule(const std::vector<Module>& modules, const QString& id) {
    for (const auto& m : modules)
        if (m.id == id) return &m;
    return nullptr;
}

// What a selection becomes. Not editing: any part of a module brings the
// whole module. Editing a module: only that module's parts can be picked.
inline QSet<QString> shapeSelection(const QSet<QString>& ids, const std::vector<Module>& modules,
                                    const QString& editingId) {
    if (!editingId.isEmpty()) {
        const Module* editing = findModule(modules, editingId);
        if (!editing) return ids;
        QSet<QString> out;
        for (const auto& id : ids)
            if (editing->memberIds.contains(id)) out.insert(id);
        return out;
    }
    QSet<QString> out = ids;
    const auto byPart = moduleByPart(modules);
    for (const auto& id : ids)
        if (const Module* m = byPart.value(id)) out.unite(m->memberIds);
    return out;
}

// While a module is edited: the parts that aren't in it are out of reach.
inline bool outsideEdit(const QString& partId, const std::vector<Module>& modules, const QString& editingId) {
    if (editingId.isEmpty()) return false;
    const Module* editing = findModule(modules, editingId);
    return editing && !editing->memberIds.contains(partId);
}

// A pinned module (not the one being edited) with a part among `ids`: the
// selection can't be moved or turned as a whole. Null when nothing is pinned.
inline const Module* pinnedAmong(const QSet<QString>& ids, const std::vector<Module>& modules,
                                 const QString& editingId) {
    for (const auto& m : modules) {
        if (!m.pinned || m.id == editingId) continue;
        if (m.memberIds.intersects(ids)) return &m;
    }
    return nullptr;
}

// Whether a part can be dragged: not out of reach, and not in a pinned
// module (unless editing it).
inline bool canDragPart(const QString& partId, const std::vector<Module>& modules, const QString& editingId) {
    if (outsideEdit(partId, modules, editingId)) return false;
    return pinnedAmong({ partId }, modules, editingId) == nullptr;
}

inline Module withPinned(Module m, bool pinned) {
    m.pinned = pinned;
    return m;
}

inline Module withMembersAdded(Module m, const QSet<QString>& ids) {
    m.memberIds.unite(ids);
    return m;
}

inline Module withMembersRemoved(Module m, const QSet<QString>& ids) {
    m.memberIds.subtract(ids);
    return m;
}

// Whether a part dropped at `area` is clear of the module's outline (no overlap at all).
inline bool outsideOutline(const QRectF& area, const QRectF& outline) {
    return area.left() >= outline.right() || area.right() <= outline.left() || area.top() >= outline.bottom()
        || area.bottom() <= outline.top();
}

// "Sam is here too", "Sam and Alex are here too", "Sam, Alex and 2 others
// are here too"; empty for nobody.
inline QString hereTooText(QStringList names) {
    names.removeDuplicates();
    const auto tr = [](const char* s) { return QCoreApplication::translate("ModuleEdit", s); };
    if (names.isEmpty()) return {};
    if (names.size() == 1) return tr("%1 is here too").arg(names[0]);
    if (names.size() == 2) return tr("%1 and %2 are here too").arg(names[0], names[1]);
    const auto rest = names.size() - 2;
    if (rest == 1) return tr("%1, %2 and one other are here too").arg(names[0], names[1]);
    return tr("%1, %2 and %3 others are here too").arg(names[0], names[1]).arg(rest);
}

}  // namespace bld::core
