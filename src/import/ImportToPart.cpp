#include "ImportToPart.h"

#include "GifWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamWriter>

#include <algorithm>

namespace bld::import {

namespace {

// Safe part-key from an arbitrary source path: filename stem with every
// non-alphanumeric / non-underscore / non-dash / non-dot char replaced
// by '_'. Collapses consecutive separators and trims trailing dots.
QString sanitizeKey(const QString& sourcePath) {
    QString stem = QFileInfo(sourcePath).completeBaseName();
    static const QRegularExpression bad(QStringLiteral("[^A-Za-z0-9_\\-.]"));
    stem.replace(bad, QStringLiteral("_"));
    static const QRegularExpression runs(QStringLiteral("_+"));
    stem.replace(runs, QStringLiteral("_"));
    while (stem.endsWith(QLatin1Char('.'))) stem.chop(1);
    if (stem.isEmpty()) stem = QStringLiteral("ImportedModel");
    return stem;
}

}  // namespace

QString writeImportedModelAsLibraryPart(
    const QString& sourceFilePath,
    QImage         renderedSprite,
    int            widthStuds,
    int            heightStuds,
    const QString& destLibraryDir,
    const QString& authorName,
    QString*       error) {
    return writeImportedModelAsLibraryPart(
        sourceFilePath, std::move(renderedSprite),
        widthStuds, heightStuds,
        destLibraryDir, authorName,
        /*connections=*/{}, error);
}

QString writeImportedModelAsLibraryPart(
    const QString& sourceFilePath,
    QImage         renderedSprite,
    int            widthStuds,
    int            heightStuds,
    const QString& destLibraryDir,
    const QString& authorName,
    const QVector<ImportedConnection>& connections,
    QString*       error) {

    if (renderedSprite.isNull() || widthStuds <= 0 || heightStuds <= 0) {
        if (error) *error = QStringLiteral("Empty sprite or zero dimensions");
        return {};
    }
    QDir dir(destLibraryDir);
    if (!dir.exists() && !QDir().mkpath(destLibraryDir)) {
        if (error) *error = QStringLiteral("Could not create library dir: %1").arg(destLibraryDir);
        return {};
    }

    QString baseKey = sanitizeKey(sourceFilePath);
    // Avoid overwriting an existing part: suffix -2, -3, ...
    QString key = baseKey;
    int     n = 1;
    while (QFile::exists(dir.filePath(key + QStringLiteral(".gif"))) ||
           QFile::exists(dir.filePath(key + QStringLiteral(".png"))) ||
           QFile::exists(dir.filePath(key + QStringLiteral(".xml")))) {
        ++n;
        key = baseKey + QStringLiteral("-") + QString::number(n);
    }

    // Sprites. A hi-res sprite goes to <key>.png (what this app draws,
    // scaled via <PixelsPerStud>) plus an 8 px/stud <key>.gif, the only
    // sprite vanilla BlueBrick loads, so the same part folder works in
    // both. An 8 px/stud sprite is written as the .gif alone.
    const QString xmlPath = dir.filePath(key + QStringLiteral(".xml"));
    const int pxPerStud = std::max(1, qRound(static_cast<double>(renderedSprite.width()) / widthStuds));
    const QString gifPath = dir.filePath(key + QStringLiteral(".gif"));
    if (pxPerStud != 8) {
        const QString pngPath = dir.filePath(key + QStringLiteral(".png"));
        if (!renderedSprite.save(pngPath, "PNG")) {
            if (error) *error = QStringLiteral("Could not write sprite to %1").arg(pngPath);
            return {};
        }
        const QImage vanilla = renderedSprite.scaled(widthStuds * 8, heightStuds * 8,
                                                     Qt::IgnoreAspectRatio,
                                                     Qt::SmoothTransformation);
        if (!writeGif(vanilla, gifPath, error)) return {};
    } else if (!writeGif(renderedSprite, gifPath, error)) {
        return {};
    }

    // <part> XML matching BlueBrickParts conventions: Author,
    // Description, and a ConnexionList when the model has free ends.
    QSaveFile xf(xmlPath);
    if (!xf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Could not open %1 for write").arg(xmlPath);
        return {};
    }
    QXmlStreamWriter w(&xf);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("part"));
    w.writeTextElement(QStringLiteral("Author"),
        authorName.isEmpty() ? QStringLiteral("Brick Layout Designer import")
                              : authorName);
    w.writeStartElement(QStringLiteral("Description"));
    w.writeTextElement(QStringLiteral("en"),
        QStringLiteral("Imported from %1 (%2 × %3 studs)")
            .arg(QFileInfo(sourceFilePath).fileName())
            .arg(widthStuds).arg(heightStuds));
    w.writeEndElement();

    // Resolution of the .png. The library scanner loads the .png when
    // this is present and the 8 px/stud .gif otherwise; vanilla ignores
    // the element. Only emitted for hi-res sprites.
    if (pxPerStud != 8) {
        w.writeTextElement(QStringLiteral("PixelsPerStud"),
                            QString::number(pxPerStud));
    }

    // <ConnexionList> so the composite part snaps like a real track
    // tile. Omitted when `connections` is empty (falls back to the
    // pure-visual tile shape).
    if (!connections.isEmpty()) {
        w.writeStartElement(QStringLiteral("ConnexionList"));
        for (const auto& c : connections) {
            w.writeStartElement(QStringLiteral("connexion"));
            w.writeTextElement(QStringLiteral("type"), c.type);
            w.writeStartElement(QStringLiteral("position"));
            w.writeTextElement(QStringLiteral("x"),
                QString::number(c.xStuds, 'f', 4));
            w.writeTextElement(QStringLiteral("y"),
                QString::number(c.yStuds, 'f', 4));
            w.writeEndElement();  // position
            w.writeTextElement(QStringLiteral("angle"),
                QString::number(c.angleDeg, 'f', 2));
            w.writeEndElement();  // connexion
        }
        w.writeEndElement();  // ConnexionList
    }

    w.writeEndElement();  // part
    w.writeEndDocument();
    if (!xf.commit()) {
        if (error) *error = QStringLiteral("Could not commit XML to %1").arg(xmlPath);
        return {};
    }
    return key;
}

}  // namespace bld::import
