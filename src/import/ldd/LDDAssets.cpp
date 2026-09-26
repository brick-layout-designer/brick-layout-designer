#include "LDDAssets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace bld::import {

bool LDDAssets::open(const QString& root) {
    outer_.reset();
    db_.reset();
    dbDir_.clear();
    ldrawXml_.clear();
    source_.clear();
    if (root.isEmpty()) return false;
    const QDir dir(root);

    for (const QString& rel : { QStringLiteral("ldraw.xml"), QStringLiteral("../ldraw.xml") }) {
        if (QFileInfo::exists(dir.filePath(rel))) {
            ldrawXml_ = QFileInfo(dir.filePath(rel)).absoluteFilePath();
            break;
        }
    }

    for (const QString& rel : { QStringLiteral("db.lif"), QStringLiteral("Assets/db.lif") }) {
        const QString path = dir.filePath(rel);
        if (!QFileInfo::exists(path)) continue;
        auto lif = std::make_unique<LifReader>();
        if (lif->open(path)) {
            db_ = std::move(lif);
            source_ = QFileInfo(path).absoluteFilePath();
            return true;
        }
    }
    for (const QString& rel : { QStringLiteral("db"), QStringLiteral("Assets/db") }) {
        const QString path = dir.filePath(rel);
        if (QFileInfo::exists(QDir(path).filePath(QStringLiteral("Materials.xml")))) {
            dbDir_ = QFileInfo(path).absoluteFilePath();
            source_ = dbDir_;
            return true;
        }
    }
    const QString assets = dir.filePath(QStringLiteral("Assets.lif"));
    if (QFileInfo::exists(assets)) {
        auto outer = std::make_unique<LifReader>();
        auto db = std::make_unique<LifReader>();
        if (outer->open(assets) && db->openNested(*outer, QStringLiteral("/db.lif"))) {
            outer_ = std::move(outer);
            db_ = std::move(db);
            source_ = QFileInfo(assets).absoluteFilePath() + QStringLiteral(" (db.lif)");
            return true;
        }
    }
    return false;
}

QByteArray LDDAssets::read(const QString& relPath) const {
    if (!dbDir_.isEmpty()) {
        QFile f(QDir(dbDir_).filePath(relPath));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    if (!db_) return {};
    // Real db.lif files store paths from the archive root; accept a
    // "/db/" prefix too, as some repackaged archives use it.
    QByteArray bytes = db_->read(QLatin1Char('/') + relPath);
    if (bytes.isEmpty()) bytes = db_->read(QStringLiteral("/db/") + relPath);
    return bytes;
}

}  // namespace bld::import
