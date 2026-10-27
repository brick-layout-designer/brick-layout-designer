#pragma once

#include "../core/ColorSpec.h"
#include "../core/FontSpec.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <vector>

// Culture-invariant primitive formatters and XML readers matching BlueBrick's
// System.Xml.Serialization.XmlSerializer + BlueBrick.MapData.XmlReadWrite behavior.
// See docs/bbm-schema.md for wire-level invariants.
namespace bld::saveload::xml {

// ---------- formatting (C# InvariantCulture ToString equivalents) ----------

QString formatBool(bool v);
QString formatInt(int v);
QString formatFloat(float v);
QString formatDouble(double v);

// ---------- readers ----------
// Each reader assumes the reader is positioned at a StartElement (e.g. from
// readNextStartElement()) and consumes through the matching EndElement.

QString readTextElement(QXmlStreamReader& r);
bool    readBoolElement(QXmlStreamReader& r);
int     readIntElement(QXmlStreamReader& r);
// A whole number for a drawn setting: any number is read (one too big for an
// int is clamped, a fraction rounded); text that isn't a finite number gives
// `fallback`.
int     readWholeElement(QXmlStreamReader& r, int fallback);
float   readFloatElement(QXmlStreamReader& r);
// A number for a drawn setting: text that isn't a finite number (NaN,
// Infinity) gives `fallback`.
float   readFloatElementOr(QXmlStreamReader& r, float fallback);
double  readDoubleElement(QXmlStreamReader& r);

// Structured primitives. Reader must be at the wrapping element's StartElement.
core::ColorSpec readColor(QXmlStreamReader& r);
QPoint  readPoint(QXmlStreamReader& r);
QPointF readPointF(QXmlStreamReader& r);
QRectF  readRectF(QXmlStreamReader& r);
core::FontSpec readFont(QXmlStreamReader& r, int dataVersion);
std::vector<float> readFloatArray(QXmlStreamReader& r);

// ---------- writers ----------

void writeBoolElement(QXmlStreamWriter& w, const QString& name, bool v);
void writeIntElement(QXmlStreamWriter& w, const QString& name, int v);
void writeFloatElement(QXmlStreamWriter& w, const QString& name, float v);
void writeDoubleElement(QXmlStreamWriter& w, const QString& name, double v);
void writeTextElement(QXmlStreamWriter& w, const QString& name, const QString& v);

void writeColor(QXmlStreamWriter& w, const QString& name, const core::ColorSpec& c);
void writePoint(QXmlStreamWriter& w, const QString& name, QPoint p);
void writePointF(QXmlStreamWriter& w, const QString& name, QPointF p);
void writeRectF(QXmlStreamWriter& w, const QString& name, const QRectF& r);
void writeFont(QXmlStreamWriter& w, const QString& name, const core::FontSpec& f);
void writeFloatArray(QXmlStreamWriter& w, const QString& name, const std::vector<float>& values);

}
