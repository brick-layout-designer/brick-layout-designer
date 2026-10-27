#include "PartListing.h"

#include <QFile>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace bld::saveload {

QString withNotListed(const QString& xml, bool notListed) {
    static const QRegularExpression tag(
        QStringLiteral(R"(\s*<NotListedInLibrary\s*/>|\s*<NotListedInLibrary>[^<]*</NotListedInLibrary>)"));
    QString out = xml;
    out.remove(tag);
    if (!notListed) return out;
    static const QRegularExpression root(QStringLiteral(R"(<(part|group)(\s[^>]*)?>)"));
    const QRegularExpressionMatch m = root.match(out);
    if (!m.hasMatch()) return out;
    out.insert(m.capturedEnd(), QStringLiteral("\n\t<NotListedInLibrary>true</NotListedInLibrary>"));
    return out;
}

bool setNotListed(const QString& filePath, bool notListed, QString* error) {
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Could not read %1").arg(filePath);
        return false;
    }
    const QString before = QString::fromUtf8(in.readAll());
    in.close();
    const QString after = withNotListed(before, notListed);
    if (after == before) return true;
    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly) || out.write(after.toUtf8()) < 0 || !out.commit()) {
        if (error) *error = QObject::tr("Could not write %1").arg(filePath);
        return false;
    }
    return true;
}

}  // namespace bld::saveload
