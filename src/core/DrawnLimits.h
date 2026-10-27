#pragma once

// The ranges of the settings the map is drawn from (grid, area, ruler and
// every sheet's own), kept when a layout is read: from a .bbm or
// .bld-layout (saveload::readBbm) and from a live layout's document
// (sync::mapFromDocJson). A value out of range is brought to the nearest
// end; one that isn't a number (NaN, infinite) gets the default. Nothing a
// layout made in either app holds is changed.
//
// Every range is inside what the server takes (fixtures/contracts/
// update-guard.json › drawingSettings.limits, checked by ContractsTest), so
// a value brought into range here, and written back to a live layout with
// the next edit, is never refused.

#include <limits>
#include <vector>

namespace bld::core {

class Map;

namespace limits {

template <typename T> struct Range {
    T min, max, fallback;
};

// Every sheet.
inline constexpr Range<int> kTransparency{ 0, 100, 100 }; // percent
inline constexpr Range<int> kHullThickness{ 1, 100, 1 };  // px
// Grid.
inline constexpr Range<int> kGridSizeInStud{ 1, std::numeric_limits<int>::max(), 32 };
inline constexpr Range<int> kSubDivisionNumber{ 2, 100, 4 };
inline constexpr Range<float> kGridThickness{ 0.0f, 100.0f, 2.0f };    // px
inline constexpr Range<float> kSubGridThickness{ 0.0f, 100.0f, 1.0f }; // px
inline constexpr Range<int> kCellIndexCorner{ -1000000, 1000000, 0 };  // cells, x and y
// Cell index types: 0 letters, 1 numbers; anything else gets the default
// (columns letters, rows numbers).
// Area.
inline constexpr Range<int> kAreaCellSizeInStud{ 1, 1024, 32 };
// Rulers.
inline constexpr Range<float> kLineThickness{ 0.0f, 100.0f, 1.0f };      // px
inline constexpr Range<float> kGuidelineThickness{ 0.0f, 100.0f, 1.0f }; // px
// Guideline dash pattern: at most 64 entries, each 0 to 1000, adding up to
// 0.01 or more; any other pattern is dropped (a solid guideline).
inline constexpr int kMaxDashEntries = 64;
inline constexpr float kMaxDash = 1000.0f;
inline constexpr double kMinDashTotal = 0.01;
// Unit: 0 studs, 1 LDU, 2 tracks, 3 modules, 4 meters, 5 feet; else studs.
inline constexpr Range<int> kRulerUnit{ 0, 5, 0 };
inline constexpr Range<double> kRulerCoordinate{ -1e7, 1e7, 0.0 }; // studs: points, centre, offset
inline constexpr Range<float> kRulerRadius{ 0.0f, 1e7f, 0.0f };    // studs
// Fonts of the grid's cell index and the rulers' measure, in points.
inline constexpr Range<float> kFontSize{ 1.0f, 500.0f, 8.25f };

} // namespace limits

// Brings every drawn setting of `map` into its range (see above).
void keepDrawnSettingsInRange(Map& map);

// A dash pattern typed in the ruler dialog, as the server takes it: lengths
// that aren't numbers dropped, each brought to 0..kMaxDash, at most
// kMaxDashEntries of them; one adding up to less than kMinDashTotal is none
// (a solid guideline).
std::vector<float> dashPatternInRange(const std::vector<float>& typed);

} // namespace bld::core
