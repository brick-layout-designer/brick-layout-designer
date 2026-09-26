#include "ArchivePath.h"

#include <QDir>

namespace bld::import {

QString resolveArchiveEntryPath(const QString& destRoot, const QString& entryName) {
    QString rel = entryName;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.isEmpty() || rel.startsWith(QLatin1Char('/'))
        || (rel.size() >= 2 && rel.at(1) == QLatin1Char(':'))) {
        return {};
    }
    const QString root = QDir::cleanPath(QDir(destRoot).absolutePath());
    const QString abs  = QDir::cleanPath(root + QLatin1Char('/') + rel);
    const QString rootWithSlash = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (!abs.startsWith(rootWithSlash)) return {};
    return abs;
}

}  // namespace bld::import
