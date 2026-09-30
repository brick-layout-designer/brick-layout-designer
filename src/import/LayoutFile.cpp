#include "LayoutFile.h"

#include "zip/SafeZip.h"
#include "zip/ZipWriter.h"

#include "../core/Map.h"
#include "../saveload/BbmReader.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SidecarIO.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace bld::import {

namespace {

const QString kManifest = QStringLiteral("manifest.json");
const QString kLayout = QStringLiteral("layout.bbm");
const QString kSidecar = QStringLiteral("sidecar.json");
const QString kFormat = QStringLiteral("bld-layout");

QString tr(const char* s) { return QCoreApplication::translate("bld::import::LayoutFile", s); }

// The image's extension as an entry name: short and plain, else "bin".
QString imageSuffix(const QString& path) {
    static const QRegularExpression plain(QStringLiteral("^[a-z0-9]{1,5}$"));
    const QString s = QFileInfo(path).suffix().toLower();
    return plain.match(s).hasMatch() ? s : QStringLiteral("bin");
}

}  // namespace

bool isLayoutFile(const QString& path) {
    return QFileInfo(path).suffix().compare(kFormat, Qt::CaseInsensitive) == 0;
}

LayoutFileResult readLayoutFile(const QString& path, const QString& assetDir) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        LayoutFileResult r;
        r.error = f.errorString();
        return r;
    }
    return readLayoutFileBytes(f.readAll(), assetDir);
}

LayoutFileResult readLayoutFileBytes(const QByteArray& bytes, const QString& assetDir) {
    LayoutFileResult r;
    const SafeZip zip(bytes);
    const auto entry = [&](const QString& name) -> std::optional<QByteArray> {
        const auto* e = zip.find(name);
        return e ? zip.read(*e) : std::nullopt;
    };
    const auto manifestBytes = zip.isValid() ? entry(kManifest) : std::nullopt;
    const QJsonObject manifest =
        manifestBytes ? QJsonDocument::fromJson(*manifestBytes).object() : QJsonObject();
    if (manifest.value(QLatin1String("format")).toString() != kFormat) {
        r.error = tr("This is not a Brick Layout Designer layout file.");
        return r;
    }
    if (manifest.value(QLatin1String("version")).toInt() > kLayoutFileVersion)
        r.warnings << tr("The file was made by a newer version of Brick Layout Designer; "
                         "anything this version doesn't know is left out.");

    const auto bbm = entry(kLayout);
    if (!bbm) {
        r.error = tr("The layout file has no readable layout in it.");
        return r;
    }
    QBuffer in;
    in.setData(*bbm);
    in.open(QIODevice::ReadOnly);
    auto loaded = saveload::readBbm(in);
    if (!loaded.ok()) {
        r.error = loaded.error;
        return r;
    }
    if (!loaded.error.isEmpty()) r.warnings << loaded.error;

    if (zip.find(kSidecar)) {
        const auto sidecarBytes = entry(kSidecar);
        const QJsonDocument doc =
            sidecarBytes ? QJsonDocument::fromJson(*sidecarBytes) : QJsonDocument();
        if (!doc.isObject()) {
            r.warnings << tr("The labels, modules, venue and background could not be read.");
        } else {
            const QJsonObject root = doc.object();
            saveload::sidecarFromJson(root, loaded.map->sidecar);
            // The background image travels inside the file: unpack it.
            const QString file =
                root.value(QLatin1String("backgroundImage")).toObject().value(QLatin1String("file")).toString();
            if (!file.isEmpty()) {
                const auto image = entry(file);
                const QString target =
                    QDir(assetDir).filePath(QString::fromLatin1(saveload::sha256Hex(image.value_or(QByteArray())))
                                            + QLatin1Char('.') + imageSuffix(file));
                bool unpacked = image.has_value() && (QFileInfo::exists(target) || [&] {
                    QDir().mkpath(assetDir);
                    QSaveFile out(target);
                    return out.open(QIODevice::WriteOnly) && out.write(*image) == image->size() && out.commit();
                }());
                if (unpacked) {
                    loaded.map->sidecar.backgroundImagePath = target;
                } else {
                    r.warnings << tr("The background image could not be unpacked.");
                    loaded.map->sidecar.backgroundImagePath.clear();
                }
            }
        }
    }
    r.map = std::move(loaded.map);
    return r;
}

QByteArray layoutFileBytes(const core::Map& map, QString* error, QStringList* warnings) {
    QBuffer bbm;
    bbm.open(QIODevice::WriteOnly);
    const auto written = saveload::writeBbm(map, bbm);
    if (!written.ok) {
        if (error) *error = written.error;
        return {};
    }
    ZipWriter zip;
    // First and stored, so the file says what it is from its first bytes.
    zip.add(kManifest,
            QJsonDocument(QJsonObject{
                              { QStringLiteral("format"), kFormat },
                              { QStringLiteral("version"), kLayoutFileVersion },
                              { QStringLiteral("generator"),
                                QStringLiteral("Brick Layout Designer %1(desktop)")
                                    .arg(QCoreApplication::applicationVersion().isEmpty()
                                             ? QString()
                                             : QCoreApplication::applicationVersion() + QLatin1Char(' ')) },
                          })
                .toJson(QJsonDocument::Compact),
            ZipWriter::Method::Stored);
    zip.add(kLayout, bbm.data());
    if (!map.sidecar.isEmpty()) {
        QJsonObject root = saveload::sidecarToJson(map.sidecar);
        root.remove(QStringLiteral("bbmHashSha256"));  // the layout is in the same file
        const QString imagePath = map.sidecar.backgroundImagePath;
        if (!imagePath.isEmpty()) {
            QFile image(imagePath);
            if (image.open(QIODevice::ReadOnly)) {
                const QString name = QStringLiteral("background.") + imageSuffix(imagePath);
                QJsonObject bg = root.value(QLatin1String("backgroundImage")).toObject();
                bg.remove(QStringLiteral("path"));
                bg[QStringLiteral("file")] = name;
                root[QStringLiteral("backgroundImage")] = bg;
                // Images are compressed already.
                zip.add(name, image.readAll(), ZipWriter::Method::Stored);
            } else if (warnings) {
                *warnings << tr("The background image %1 could not be read, so the file only "
                                "keeps its path.")
                                 .arg(imagePath);
            }
        }
        zip.add(kSidecar, QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
    return zip.finish();
}

bool writeLayoutFile(const core::Map& map, const QString& path, QString* error, QStringList* warnings) {
    const QByteArray bytes = layoutFileBytes(map, error, warnings);
    if (bytes.isEmpty()) return false;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

}  // namespace bld::import
