#pragma once

// Cross-brick connection-graph refresh. Run after any mutation that changes
// brick positions / orientations / membership so each brick's stored
// linkedToId reflects physical reality: linked iff the two connection
// points actually coincide in world coords. Without this, moving a brick
// leaves stale linkedToId values and the snap / display logic sees
// connections as "linked" when they're actually free.

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

// Walk every brick, verify every linkedToId against the partner brick's
// current world connection position, and both:
//   * clear links whose world positions no longer coincide
//   * re-link free-to-free pairs whose world positions DO coincide and
//     whose types match
//
// A link is stored as the partner CONNECTION's id (like vanilla's
// <LinkedTo>); connection points without an id get one.
//
// Tolerance is 0.5 studs (quarter of a brick unit).
void rebuildConnectivity(core::Map& map, parts::PartsLibrary& lib);

}  // namespace bld::edit
