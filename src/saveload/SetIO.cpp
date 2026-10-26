#include "SetIO.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamWriter>

namespace bld::saveload {

bool writeSetXml(const QString& filePath,
                 const SetManifest& manifest,
                 QString* error) {
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Cannot open %1 for write").arg(filePath);
        return false;
    }
    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(1);
    // BrickTracks set files use tab indentation — match for a tidy diff
    // against any hand-authored sets in the same library.
    w.setAutoFormattingIndent(-1);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("group"));

    if (!manifest.author.isEmpty())
        w.writeTextElement(QStringLiteral("Author"), manifest.author);

    if (!manifest.name.isEmpty()) {
        w.writeStartElement(QStringLiteral("Description"));
        w.writeTextElement(QStringLiteral("en"), manifest.name);
        w.writeEndElement();  // Description
    }

    if (!manifest.sortingKey.isEmpty())
        w.writeTextElement(QStringLiteral("SortingKey"), manifest.sortingKey);

    w.writeTextElement(QStringLiteral("CanUngroup"),
                       manifest.canUngroup ? QStringLiteral("true")
                                           : QStringLiteral("false"));

    w.writeStartElement(QStringLiteral("SubPartList"));
    for (const SetSubpart& sp : manifest.subparts) {
        w.writeStartElement(QStringLiteral("SubPart"));
        w.writeAttribute(QStringLiteral("id"), sp.partKey);
        w.writeStartElement(QStringLiteral("position"));
        w.writeTextElement(QStringLiteral("x"),
                           QString::number(sp.positionStuds.x(), 'f', 6));
        w.writeTextElement(QStringLiteral("y"),
                           QString::number(sp.positionStuds.y(), 'f', 6));
        w.writeEndElement();  // position
        w.writeTextElement(QStringLiteral("angle"),
                           QString::number(sp.angleDegrees, 'f', 4));
        w.writeEndElement();  // SubPart
    }
    w.writeEndElement();  // SubPartList

    w.writeEndElement();  // group
    w.writeEndDocument();

    if (!f.commit()) {
        if (error) *error = QStringLiteral("Could not commit %1").arg(filePath);
        return false;
    }
    return true;
}

namespace {

// Text for an XML element: escaped, without the control characters XML 1.0
// doesn't allow (a pasted name with one made a file BlueBrick won't open).
QString escapedText(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (u < 0x20 && u != u'\t' && u != u'\n' && u != u'\r') continue;
        if (u == 0xFFFE || u == 0xFFFF) continue;
        if (c == QLatin1Char('&')) out += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<')) out += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>')) out += QStringLiteral("&gt;");
        else out += c;
    }
    return out;
}

}  // namespace

bool renameSetXml(const QString& filePath, const QString& name, QString* error) {
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot read %1").arg(filePath);
        return false;
    }
    QString xml = QString::fromUtf8(in.readAll());
    in.close();
    static const QRegularExpression group(QStringLiteral(R"(<group\b[^>]*>)"));
    const QRegularExpressionMatch top = group.match(xml);
    if (!top.hasMatch()) {
        if (error) *error = QStringLiteral("%1 is not a set").arg(filePath);
        return false;
    }
    const QString en = QStringLiteral("<en>%1</en>").arg(escapedText(name));
    static const QRegularExpression description(QStringLiteral(R"(<Description\b[^>]*>(.*?)</Description>)"),
                                                QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression english(QStringLiteral(R"(<en\s*/>|<en\b[^>]*>.*?</en>)"),
                                            QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch desc = description.match(xml);
    if (desc.hasMatch()) {
        const QRegularExpressionMatch old = english.match(xml, desc.capturedStart(1));
        if (old.hasMatch() && old.capturedEnd() <= desc.capturedEnd(1))
            xml.replace(old.capturedStart(), old.capturedLength(), en);
        else
            xml.insert(desc.capturedStart(1), en);
    } else {
        xml.insert(top.capturedEnd(), QStringLiteral("\n\t<Description>\n\t\t%1\n\t</Description>").arg(en));
    }
    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly) || out.write(xml.toUtf8()) < 0 || !out.commit()) {
        if (error) *error = QStringLiteral("Could not write %1").arg(filePath);
        return false;
    }
    return true;
}

}  // namespace bld::saveload
