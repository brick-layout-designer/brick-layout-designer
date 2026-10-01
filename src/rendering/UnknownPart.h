#pragma once

// A part the library doesn't know, drawn as vanilla BlueBrick draws it
// (BrickLibrary.cs createUnknownImage) and as the web does
// (render/unknownPart.ts): a picture the brick's stored size in whole
// studs, a red cross corner to corner, and the part number in black in
// the middle, turned with the brick. render-parity/parts.json holds the
// numbers both apps are tested against.

#include <QString>

class QGraphicsItem;

namespace bld::rendering {

struct UnknownPartLook {
    double width = 0;   // scene px, whole studs
    double height = 0;
    double penPx = 0;   // the cross's pen
    double fontPx = 0;  // the part number (vanilla's points at 96 dpi)
};

UnknownPartLook unknownPartLook(const QString& partNumber, double widthStuds, double heightStuds);

// The cross and the part number as children of `parent`, centred on its origin.
void addUnknownPartDrawing(QGraphicsItem* parent, const QString& partNumber, const UnknownPartLook& look);

}  // namespace bld::rendering
