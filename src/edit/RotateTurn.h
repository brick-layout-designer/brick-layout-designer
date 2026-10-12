#pragma once

// BlueBrick's RotateBrick for a selection joined to other track: instead of
// turning by the rotation step, the selection steps round its connection to
// the fixed track, to its next free connection of that type, so it stays
// connected (Actions/Bricks/RotateBrick.cs).

#include "../core/Brick.h"

#include <QPointF>
#include <QString>

#include <optional>
#include <vector>

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

struct ConnectedTurn {
    double  degrees = 0;  // the turn that joins the new connection to the fixed one
    QString brickGuid;    // the picked part whose connection now joins
    int     connection = -1;
    QPointF at;           // where the joined connection stays (studs)
};

// `selected` in BlueBrick's order (here: sheet order). `steps` > 0 goes on
// to the next connection, < 0 back. No value when nothing outside the
// selection is joined to it.
std::optional<ConnectedTurn> connectedTurn(const core::Map& map, const std::vector<const core::Brick*>& selected, int steps,
                                           parts::PartsLibrary& lib);

}  // namespace bld::edit
