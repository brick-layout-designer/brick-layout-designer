#pragma once

// Lengths in the Venue Designer: typed in feet and inches the way people
// write them on a sketch (12'6", 40′ 4″, 129, 6 1/2"), or metric, or studs;
// shown back in the chosen unit. Venues store studs (1 stud = 8 mm). The
// twin of the web's venues/designer/units.ts; both tests check the same
// cases.

#include <QString>

#include <optional>

namespace bld::edit::venue {

constexpr double kStudsPerInch = 38.09814081 / 12.0;
constexpr double kStudsPerMm = 1.0 / 8.0;

enum class LengthUnit { FeetInches, Metres, Studs };

// Studs for a typed length, or nullopt. A bare number is inches in
// feet-and-inches mode, metres in metric mode, studs in stud mode.
std::optional<double> parseLength(const QString& text, LengthUnit unit = LengthUnit::FeetInches);

// 12′ 6½″ (to the quarter inch), 3.81 m, or 145.3 studs.
QString formatLength(double studs, LengthUnit unit = LengthUnit::FeetInches);

// Degrees clockwise from east, 0–359.
QString formatAngle(double deg);

} // namespace bld::edit::venue
