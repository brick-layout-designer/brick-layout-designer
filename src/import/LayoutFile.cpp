#include "LayoutFile.h"

#include "zip/SafeZip.h"
#include "zip/ZipWriter.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
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
#include <QSet>

namespace bld::import {

namespace {

const QString kManifest = QStringLiteral("manifest.json");
const QString kLayout = QStringLiteral("layout.bbm");
const QString kSidecar = QStringLiteral("sidecar.json");
const QString kFormat = QStringLiteral("bld-layout");
const QString kParts = QStringLiteral("parts/");
const QStringList kSpriteSuffixes{ QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("jpg"),
                                   QStringLiteral("jpeg") };

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeAll(const QString& path, const QByteArray& data) {
    QSaveFile out(path);
    return out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit();
}

QString tr(const char* s) { return QCoreApplication::translate("bld::import::LayoutFile", s); }

// The image's extension as an entry name: short and plain, else "bin".
QString imageSuffix(const QString& path) {
    static const QRegularExpression plain(QStringLiteral("^[a-z0-9]{1,5}$"));
    const QString s = QFileInfo(path).suffix().toLower();
    return plain.match(s).hasMatch() ? s : QStringLiteral("bin");
}

}  // namespace

bool isLayoutPartFileName(const QString& name) {
    static const QRegularExpression ok(
        QStringLiteral(R"(^[^./\\:*?"<>|\x00-\x1F][^/\\:*?"<>|\x00-\x1F]{0,199}\.(xml|png|gif|jpe?g)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return ok.match(name).hasMatch() && !name.contains(QStringLiteral(".."));
}

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
    for (const auto& e : zip.entries()) {
        if (!e.name.startsWith(kParts) || e.isDir) continue;
        const QString name = e.name.mid(kParts.size());
        const auto data = isLayoutPartFileName(name) ? zip.read(e) : std::nullopt;
        if (data) r.partFiles.insert(name, *data);
        else r.warnings << tr("The part file %1 in the layout could not be read.").arg(name);
    }
    r.map = std::move(loaded.map);
    return r;
}

QMap<QString, QByteArray> layoutPartFiles(const core::Map& map, const parts::PartsLibrary& library,
                                          const QString& standardRoot) {
    QStringList todo;
    for (const auto& layer : map.layers()) {
        if (!layer || layer->kind() != core::LayerKind::Brick) continue;
        const auto& bricks = static_cast<const core::LayerBrick&>(*layer);
        for (const auto& b : bricks.bricks) todo << b.partNumber;
        for (const auto& g : bricks.groups)
            if (!g.partNumber.isEmpty()) todo << g.partNumber;
    }
    const QString root = QFileInfo(standardRoot).canonicalFilePath();
    QMap<QString, QByteArray> files;
    QSet<QString> seen;
    while (!todo.isEmpty()) {
        const QString key = todo.takeLast().toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        const auto meta = library.metadata(key);
        if (!meta) continue;
        for (const auto& sub : meta->subparts) todo << sub.subKey;  // a set needs its parts
        const QFileInfo xml(meta->xmlFilePath);
        const QString path = xml.canonicalFilePath();
        if (path.isEmpty() || (!root.isEmpty() && path.startsWith(root + QLatin1Char('/')))) continue;
        files.insert(xml.fileName(), readAll(path));
        for (const auto& suffix : kSpriteSuffixes) {
            const QFileInfo sprite(xml.dir().filePath(xml.completeBaseName() + QLatin1Char('.') + suffix));
            if (sprite.exists()) files.insert(sprite.fileName(), readAll(sprite.filePath()));
        }
    }
    return files;
}

LayoutPartsInstall installLayoutParts(const QMap<QString, QByteArray>& files, const QString& dir,
                                      const parts::PartsLibrary& library) {
    LayoutPartsInstall r;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (!it.key().endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive) || !isLayoutPartFileName(it.key()))
            continue;
        const QString stem = it.key().chopped(4);
        // A set's file is <PartNumber>.<Color>.set.xml.
        const QString key = stem.endsWith(QStringLiteral(".set"), Qt::CaseInsensitive) ? stem.chopped(4) : stem;
        if (const auto meta = library.metadata(key)) {
            if (readAll(meta->xmlFilePath) != it.value()) r.differing << key;
            continue;
        }
        QDir().mkpath(dir);
        bool written = true;
        for (auto f = files.constBegin(); f != files.constEnd(); ++f) {
            if (QFileInfo(f.key()).completeBaseName().compare(stem, Qt::CaseInsensitive) != 0) continue;
            if (!writeAll(QDir(dir).filePath(f.key()), f.value())) {
                r.failed << f.key();
                written = false;
            }
        }
        if (written) r.newParts << QDir(dir).filePath(it.key());
    }
    return r;
}

QMap<QString, QByteArray> filesOfPart(const QMap<QString, QByteArray>& files, const QString& key) {
    QMap<QString, QByteArray> out;
    const QString set = key + QStringLiteral(".set");
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString base = QFileInfo(it.key()).completeBaseName();
        if (base.compare(key, Qt::CaseInsensitive) == 0 || base.compare(set, Qt::CaseInsensitive) == 0)
            out.insert(it.key(), it.value());
    }
    return out;
}

QMap<QString, QByteArray> libraryFilesOfPart(const parts::PartsLibrary& library, const QString& key) {
    QMap<QString, QByteArray> out;
    const auto meta = library.metadata(key);
    if (!meta) return out;
    const QFileInfo xml(meta->xmlFilePath);
    out.insert(xml.fileName(), readAll(xml.filePath()));
    for (const auto& suffix : kSpriteSuffixes) {
        const QFileInfo sprite(xml.dir().filePath(xml.completeBaseName() + QLatin1Char('.') + suffix));
        if (sprite.exists()) out.insert(sprite.fileName(), readAll(sprite.filePath()));
    }
    return out;
}

bool replaceLocalPart(const QString& localXmlPath, const QMap<QString, QByteArray>& files,
                      const QString& backupDir, QString* error) {
    const QFileInfo xml(localXmlPath);
    const QDir dir = xml.dir();
    const QString stem = xml.completeBaseName();
    QStringList old{ xml.fileName() };
    for (const auto& suffix : kSpriteSuffixes) {
        const QString name = stem + QLatin1Char('.') + suffix;
        if (dir.exists(name)) old << name;
    }
    // The backup first: nothing of the old part goes before it's safe.
    QDir().mkpath(backupDir);
    for (const auto& name : old) {
        if (!writeAll(QDir(backupDir).filePath(name), readAll(dir.filePath(name)))) {
            if (error) *error = tr("Could not back up %1.").arg(dir.filePath(name));
            return false;
        }
    }
    // The layout's files under this part's names (its case may differ).
    QMap<QString, QByteArray> incoming;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString suffix = QFileInfo(it.key()).suffix().toLower();
        incoming.insert(suffix == QLatin1String("xml") ? xml.fileName() : stem + QLatin1Char('.') + suffix, it.value());
    }
    for (const auto& name : old)
        if (!incoming.contains(name)) QFile::remove(dir.filePath(name));
    for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it) {
        if (!writeAll(dir.filePath(it.key()), it.value())) {
            if (error) *error = tr("Could not write %1.").arg(dir.filePath(it.key()));
            return false;
        }
    }
    return true;
}

QString unusedPartKey(const QString& key, const parts::PartsLibrary& library) {
    const int dot = key.lastIndexOf(QLatin1Char('.'));
    const bool hasColor = dot > 0 && dot < key.size() - 1;
    const QString number = hasColor ? key.left(dot) : key;
    const QString color = hasColor ? key.mid(dot) : QString();
    for (int n = 2;; ++n) {
        const QString candidate = number + QLatin1Char('-') + QString::number(n) + color;
        if (!library.metadata(candidate)) return candidate;
    }
}

QString installPartAs(const QMap<QString, QByteArray>& files, const QString& stem, const QString& newStem,
                      const QString& dir) {
    QDir().mkpath(dir);
    QString xmlPath;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QFileInfo f(it.key());
        if (f.completeBaseName().compare(stem, Qt::CaseInsensitive) != 0) continue;
        const QString suffix = f.suffix().toLower();
        const QString path = QDir(dir).filePath(newStem + QLatin1Char('.') + suffix);
        if (!writeAll(path, it.value())) return {};
        if (suffix == QLatin1String("xml")) xmlPath = path;
    }
    return xmlPath;
}

int renamePartInMap(core::Map& map, const QString& from, const QString& to) {
    int changed = 0;
    for (auto& layer : map.layers()) {
        if (!layer || layer->kind() != core::LayerKind::Brick) continue;
        auto& bricks = static_cast<core::LayerBrick&>(*layer);
        for (auto& b : bricks.bricks) {
            if (b.partNumber.compare(from, Qt::CaseInsensitive) != 0) continue;
            b.partNumber = to;
            ++changed;
        }
        for (auto& g : bricks.groups) {
            if (g.partNumber.isEmpty() || g.partNumber.compare(from, Qt::CaseInsensitive) != 0) continue;
            g.partNumber = to;
            ++changed;
        }
    }
    return changed;
}

QByteArray layoutFileBytes(const core::Map& map, QString* error, QStringList* warnings,
                           const QMap<QString, QByteArray>& partFiles) {
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
    for (auto it = partFiles.constBegin(); it != partFiles.constEnd(); ++it) {
        if (!isLayoutPartFileName(it.key())) continue;
        const bool xml = it.key().endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive);
        zip.add(kParts + it.key(), it.value(), xml ? ZipWriter::Method::Deflated : ZipWriter::Method::Stored);
    }
    return zip.finish();
}

bool writeLayoutFile(const core::Map& map, const QString& path, QString* error, QStringList* warnings,
                     const QMap<QString, QByteArray>& partFiles) {
    const QByteArray bytes = layoutFileBytes(map, error, warnings, partFiles);
    if (bytes.isEmpty()) return false;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

}  // namespace bld::import
