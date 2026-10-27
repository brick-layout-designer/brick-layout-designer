#include "SetVersion.h"

#include <QCryptographicHash>

#include <cmath>

namespace bld::parts {

namespace {

QString milli(double v) {
    return QString::number(static_cast<qint64>(std::floor(v * 1000.0 + 0.5)));
}

// The first 16 hex digits of the text's SHA-256 (UTF-8).
QString digest16(const QString& text) {
    const QByteArray digest = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex().left(16));
}

} // namespace

QString setVersionText(const PartMetadata& set) {
    QString name;
    for (const PartDescription& d : set.descriptions)
        if (d.language == QLatin1String("en")) {
            name = d.text.trimmed();
            break;
        }
    QString text = QStringLiteral("set-v1\n") + name + QLatin1Char('\n');
    for (const PartSubPart& sp : set.subparts)
        text += sp.subKey.toLower() + QLatin1Char('|') + milli(sp.position.x()) + QLatin1Char('|')
                + milli(sp.position.y()) + QLatin1Char('|') + milli(sp.angleDegrees) + QLatin1Char('\n');
    return text;
}

QString setVersion(const PartMetadata& set) {
    if (set.kind != PartKind::Group || set.subparts.isEmpty()) return {};
    return digest16(setVersionText(set));
}

QString nestedSetVersion(const QString& own, const QList<InnerSetVersion>& inner) {
    if (inner.isEmpty()) return own;
    QString text = QStringLiteral("set-v2\n") + own + QLatin1Char('\n');
    for (const InnerSetVersion& i : inner) text += i.subKey.toLower() + QLatin1Char('|') + i.version + QLatin1Char('\n');
    return digest16(text);
}

} // namespace bld::parts
