#include "ModuleThumbnail.h"

#include "SavedViews.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../saveload/BbmReader.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace bld::ui {

namespace {
bool hasParts(const core::Map& map) {
    for (const auto& l : map.layers()) {
        const auto* b = dynamic_cast<const core::LayerBrick*>(l.get());
        if (b && !b->bricks.empty()) return true;
    }
    return false;
}
}  // namespace

QImage renderModuleThumbnail(const QString& bbmPath, parts::PartsLibrary& parts, int side) {
    auto loaded = saveload::readBbm(bbmPath);
    if (!loaded.ok()) return {};
    return renderModuleThumbnail(*loaded.map, parts, side);
}

QImage renderModuleThumbnail(const core::Map& module, parts::PartsLibrary& parts, int side) {
    if (!hasParts(module)) return {};
    views::PictureRenderer renderer(module, parts);
    const auto spec = views::viewPicture(views::wholeLayout(), module, &renderer.builder());
    if (!spec.has_value()) return {};
    const double scale = views::scaleForSide(spec->region, side);
    return renderer.render(*spec, views::pictureSize(spec->region, scale));
}

QImage moduleThumbnail(const QString& bbmPath, parts::PartsLibrary& parts, const QString& cacheDir, int side) {
    const QFileInfo fi(bbmPath);
    if (!fi.exists()) return {};
    QString dir = cacheDir;
    if (dir.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!base.isEmpty()) dir = base + QStringLiteral("/module-thumbnails");
    }
    const QByteArray key = QCryptographicHash::hash(
        (fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.size()) + QLatin1Char('|') +
         QString::number(fi.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|') + QString::number(side))
            .toUtf8(),
        QCryptographicHash::Sha1).toHex();
    const QString cached = dir.isEmpty() ? QString() : dir + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".png");
    if (!cached.isEmpty()) {
        QImage hit(cached);
        if (!hit.isNull()) return hit;
    }
    QImage img = renderModuleThumbnail(bbmPath, parts, side);
    if (!img.isNull() && !cached.isEmpty() && QDir().mkpath(dir)) img.save(cached, "PNG");
    return img;
}

}  // namespace bld::ui
