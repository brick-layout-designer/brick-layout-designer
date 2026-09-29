#include "VenueUnits.h"

#include <QRegularExpression>

#include <cmath>

namespace bld::edit::venue {

namespace {

const QString kNum = QStringLiteral(R"((\d+(?:\.\d+)?))");
const QString kFrac = QStringLiteral(R"((?:\s+(\d+)\/(\d+))?)");

// Whole-match regex, compiled once per pattern.
QRegularExpressionMatch whole(const QString& pattern, const QString& text) {
    const QRegularExpression re(QStringLiteral("^") + pattern + QStringLiteral("$"));
    return re.match(text);
}

// n + a/b, or nullopt for a zero denominator.
std::optional<double> withFraction(double n, const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return n;
    const int d = b.toInt();
    if (d == 0) return std::nullopt;
    return n + a.toInt() / static_cast<double>(d);
}

} // namespace

std::optional<double> parseLength(const QString& input, LengthUnit unit) {
    QString t = input.trimmed().toLower();
    t.replace(QChar(0x2032), QLatin1Char('\'')).replace(QChar(0x2019), QLatin1Char('\''));
    t.replace(QChar(0x2033), QLatin1Char('"')).replace(QChar(0x201D), QLatin1Char('"'));
    t.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    if (t.isEmpty()) return std::nullopt;

    if (const auto m = whole(kNum + QStringLiteral(R"(\s*(mm|cm|m))"), t); m.hasMatch()) {
        const QString u = m.captured(2);
        const double mm = m.captured(1).toDouble()
                          * (u == QLatin1String("m")    ? 1000.0
                             : u == QLatin1String("cm") ? 10.0
                                                        : 1.0);
        return mm * kStudsPerMm;
    }
    if (const auto m = whole(kNum + QStringLiteral(R"(\s*(studs?|st))"), t); m.hasMatch())
        return m.captured(1).toDouble();

    // feet [inches] with ' / ft / feet and " / in / inch(es)
    if (const auto m = whole(QStringLiteral("(?:") + kNum + QStringLiteral(R"(\s*(?:'|ft|feet|foot))?\s*(?:)")
                                 + kNum + kFrac + QStringLiteral(R"(\s*(?:"|in|inch|inches))?)"),
                             t);
        m.hasMatch() && (m.hasCaptured(1) || m.hasCaptured(2))) {
        const double feet = m.hasCaptured(1) ? m.captured(1).toDouble() : 0.0;
        const auto inches =
            withFraction(m.hasCaptured(2) ? m.captured(2).toDouble() : 0.0, m.captured(3), m.captured(4));
        if (!inches) return std::nullopt;
        return (feet * 12.0 + *inches) * kStudsPerInch;
    }
    // feet then a bare inch count: 12' 6, 12'6 1/2
    if (const auto m = whole(kNum + QStringLiteral(R"(\s*'\s*)") + kNum + kFrac, t); m.hasMatch()) {
        const auto inches = withFraction(m.captured(2).toDouble(), m.captured(3), m.captured(4));
        if (!inches) return std::nullopt;
        return (m.captured(1).toDouble() * 12.0 + *inches) * kStudsPerInch;
    }
    if (const auto m = whole(kNum + kFrac, t); m.hasMatch()) {
        const auto v = withFraction(m.captured(1).toDouble(), m.captured(2), m.captured(3));
        if (!v) return std::nullopt;
        switch (unit) {
        case LengthUnit::Metres: return *v * 1000.0 * kStudsPerMm;
        case LengthUnit::Studs: return *v;
        case LengthUnit::FeetInches: return *v * kStudsPerInch;
        }
    }
    return std::nullopt;
}

QString formatLength(double studs, LengthUnit unit) {
    if (unit == LengthUnit::Metres)
        return QStringLiteral("%1 m").arg(studs / kStudsPerMm / 1000.0, 0, 'f', 2);
    if (unit == LengthUnit::Studs) return QStringLiteral("%1 studs").arg(std::round(studs * 10.0) / 10.0);
    static const QString kFractions[] = { QString(), QStringLiteral("¼"), QStringLiteral("½"),
                                          QStringLiteral("¾") };
    const long quarters = std::lround(std::abs(studs) / kStudsPerInch * 4.0);
    const QString sign = studs < 0 && quarters > 0 ? QStringLiteral("−") : QString();
    const long feet = quarters / 48;
    const long rest = quarters - feet * 48;
    const QString inches = QString::number(rest / 4) + kFractions[rest % 4];
    if (feet == 0) return sign + inches + QStringLiteral("″");
    return QStringLiteral("%1%2′ %3″").arg(sign).arg(feet).arg(inches);
}

QString formatAngle(double deg) {
    const long d = ((std::lround(deg) % 360) + 360) % 360;
    return QStringLiteral("%1°").arg(d);
}

} // namespace bld::edit::venue
