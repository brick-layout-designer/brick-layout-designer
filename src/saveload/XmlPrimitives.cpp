#include "XmlPrimitives.h"

#include <QLocale>

namespace bld::saveload::xml {

namespace {

// C# InvariantCulture for numbers is equivalent to QLocale::c() with 'g' format.
// Default precisions: float -> 7, double -> 15. These match .NET's default
// ToString() for floats/doubles in G7/G15 modes. The "R" specifier (round-trip)
// used at a few call sites upstream produces higher precision when needed; we
// detect those per-call-site when goldens disagree.
constexpr int kFloatPrecision  = 7;
constexpr int kDoublePrecision = 15;

QString formatInvariantDouble(double v, int precision) {
    // C# "G" format trims trailing zeros; QString::number with 'g' does the
    // same. .NET writes the exponent with an upper-case E ("-4.371139E-08").
    QString s = QString::number(v, 'g', precision);
    s.replace(QLatin1Char('e'), QLatin1Char('E'));
    return s;
}

}

QString formatBool(bool v)     { return v ? QStringLiteral("true") : QStringLiteral("false"); }
QString formatInt(int v)       { return QString::number(v); }
QString formatFloat(float v)   { return formatInvariantDouble(static_cast<double>(v), kFloatPrecision); }
QString formatDouble(double v) { return formatInvariantDouble(v, kDoublePrecision); }

// ---------- readers ----------

QString readTextElement(QXmlStreamReader& r) {
    return r.readElementText();
}

bool readBoolElement(QXmlStreamReader& r) {
    const QString s = r.readElementText().trimmed();
    return s.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || s == QStringLiteral("1");
}

int readIntElement(QXmlStreamReader& r) {
    return r.readElementText().toInt();
}

float readFloatElement(QXmlStreamReader& r) {
    bool ok = false;
    const float v = r.readElementText().toFloat(&ok);
    return ok ? v : 0.0f;
}

double readDoubleElement(QXmlStreamReader& r) {
    bool ok = false;
    const double v = r.readElementText().toDouble(&ok);
    return ok ? v : 0.0;
}

core::ColorSpec readColor(QXmlStreamReader& r) {
    // <Wrapper><IsKnownColor>bool</IsKnownColor><Name>string</Name></Wrapper>
    bool isKnown = false;
    QString name;
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("IsKnownColor")) {
            isKnown = readBoolElement(r);
        } else if (r.name() == QStringLiteral("Name")) {
            name = readTextElement(r);
        } else {
            r.skipCurrentElement();
        }
    }
    if (isKnown) {
        // Preserve the knownName verbatim so writes round-trip even when Qt can't
        // resolve the KnownColor enum value to an identical QColor (System.Drawing's
        // KnownColor palette is a superset of what QColor::fromString recognizes).
        const QColor resolved(name);
        return core::ColorSpec::fromKnown(resolved.isValid() ? resolved : QColor(Qt::black), name);
    }
    // Unknown color: `name` is a signed 32-bit ARGB as hex or decimal.
    bool ok = false;
    const auto argb = static_cast<quint32>(name.toLongLong(&ok, 16));
    if (ok) return core::ColorSpec::fromArgb(QColor::fromRgba(argb));
    const auto dec = static_cast<quint32>(name.toLongLong(&ok, 10));
    if (ok) return core::ColorSpec::fromArgb(QColor::fromRgba(dec));
    return {};
}

QPoint readPoint(QXmlStreamReader& r) {
    int x = 0, y = 0;
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("X")) x = readIntElement(r);
        else if (r.name() == QStringLiteral("Y")) y = readIntElement(r);
        else r.skipCurrentElement();
    }
    return { x, y };
}

QPointF readPointF(QXmlStreamReader& r) {
    float x = 0.0f, y = 0.0f;
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("X") || r.name() == QStringLiteral("x")) x = readFloatElement(r);
        else if (r.name() == QStringLiteral("Y") || r.name() == QStringLiteral("y")) y = readFloatElement(r);
        else r.skipCurrentElement();
    }
    return { x, y };
}

QRectF readRectF(QXmlStreamReader& r) {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("X"))      x = readFloatElement(r);
        else if (n == QStringLiteral("Y"))      y = readFloatElement(r);
        else if (n == QStringLiteral("Width"))  w = readFloatElement(r);
        else if (n == QStringLiteral("Height")) h = readFloatElement(r);
        else r.skipCurrentElement();
    }
    return { x, y, w, h };
}

std::vector<float> readFloatArray(QXmlStreamReader& r) {
    // Upstream writes <Wrapper><value>...</value>*</Wrapper>.
    std::vector<float> out;
    while (r.readNextStartElement()) {
        if (r.name() == QStringLiteral("value")) out.push_back(readFloatElement(r));
        else r.skipCurrentElement();
    }
    return out;
}

core::FontSpec readFont(QXmlStreamReader& r, int dataVersion) {
    // <Wrapper><FontFamily>...</FontFamily><Size>float</Size><Style>string</Style>? </Wrapper>
    // Style element is v6+ in upstream data.
    core::FontSpec f;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if      (n == QStringLiteral("FontFamily")) f.familyName  = readTextElement(r);
        else if (n == QStringLiteral("Size"))       f.sizePt      = readFloatElement(r);
        else if (n == QStringLiteral("Style"))      f.styleString = readTextElement(r);
        else r.skipCurrentElement();
    }
    if (dataVersion < 6) f.styleString = QStringLiteral("Regular");
    return f;
}

// ---------- writers ----------

void writeBoolElement(QXmlStreamWriter& w, const QString& name, bool v) {
    w.writeTextElement(name, formatBool(v));
}

void writeIntElement(QXmlStreamWriter& w, const QString& name, int v) {
    w.writeTextElement(name, formatInt(v));
}

void writeFloatElement(QXmlStreamWriter& w, const QString& name, float v) {
    w.writeTextElement(name, formatFloat(v));
}

void writeDoubleElement(QXmlStreamWriter& w, const QString& name, double v) {
    w.writeTextElement(name, formatDouble(v));
}

void writeTextElement(QXmlStreamWriter& w, const QString& name, const QString& v) {
    w.writeTextElement(name, v);
}

void writeColor(QXmlStreamWriter& w, const QString& name, const core::ColorSpec& c) {
    w.writeStartElement(name);
    writeBoolElement(w, QStringLiteral("IsKnownColor"), c.isKnown());
    if (c.isKnown()) {
        writeTextElement(w, QStringLiteral("Name"), c.knownName);
    } else {
        const quint32 argb = (static_cast<quint32>(c.color.alpha()) << 24)
                           | (static_cast<quint32>(c.color.red())   << 16)
                           | (static_cast<quint32>(c.color.green()) << 8)
                           | static_cast<quint32>(c.color.blue());
        // Vanilla emits lowercase hex: int.ToString("x") (we observed "ffffffff" style).
        writeTextElement(w, QStringLiteral("Name"),
                         QStringLiteral("%1").arg(argb, 8, 16, QLatin1Char('0')));
    }
    w.writeEndElement();
}

void writePoint(QXmlStreamWriter& w, const QString& name, QPoint p) {
    w.writeStartElement(name);
    writeIntElement(w, QStringLiteral("X"), p.x());
    writeIntElement(w, QStringLiteral("Y"), p.y());
    w.writeEndElement();
}

void writePointF(QXmlStreamWriter& w, const QString& name, QPointF p) {
    w.writeStartElement(name);
    writeFloatElement(w, QStringLiteral("X"), static_cast<float>(p.x()));
    writeFloatElement(w, QStringLiteral("Y"), static_cast<float>(p.y()));
    w.writeEndElement();
}

void writeRectF(QXmlStreamWriter& w, const QString& name, const QRectF& r) {
    w.writeStartElement(name);
    writeFloatElement(w, QStringLiteral("X"), static_cast<float>(r.x()));
    writeFloatElement(w, QStringLiteral("Y"), static_cast<float>(r.y()));
    writeFloatElement(w, QStringLiteral("Width"),  static_cast<float>(r.width()));
    writeFloatElement(w, QStringLiteral("Height"), static_cast<float>(r.height()));
    w.writeEndElement();
}

void writeFloatArray(QXmlStreamWriter& w, const QString& name, const std::vector<float>& values) {
    w.writeStartElement(name);
    for (float v : values) writeFloatElement(w, QStringLiteral("value"), v);
    w.writeEndElement();
}

void writeFont(QXmlStreamWriter& w, const QString& name, const core::FontSpec& f) {
    w.writeStartElement(name);
    writeTextElement (w, QStringLiteral("FontFamily"), f.familyName);
    writeFloatElement(w, QStringLiteral("Size"),       f.sizePt);
    writeTextElement (w, QStringLiteral("Style"),      f.styleString);
    w.writeEndElement();
}

}
