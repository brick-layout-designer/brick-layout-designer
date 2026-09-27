#pragma once

#include "ColorSpec.h"
#include "FontSpec.h"
#include "Layer.h"

#include <QPoint>
#include <QString>

namespace bld::core {

enum class CellIndexType {
    Letters = 0,
    Numbers = 1,
};

class LayerGrid : public Layer {
public:
    LayerKind kind() const override { return LayerKind::Grid; }

    // BlueBrick's AlphabeticIndex: the label of the n-th cell from the
    // index origin. Letters: 1 -> "A", 26 -> "Z", 27 -> "AA". The origin
    // (0) and cells before it have no label.
    static QString cellIndexLabel(int n, bool letters) {
        if (n <= 0) return {};
        if (!letters) return QString::number(n);
        QString out;
        int rest = n;
        do {
            out.prepend(QChar(u'A' + (rest - 1) % 26));
            if (rest % 26 == 0) rest = rest / 26 - 1;
            else rest /= 26;
        } while (rest > 0);
        return out;
    }

    ColorSpec gridColor     = ColorSpec::fromArgb(QColor(0, 0, 0, 128));
    float     gridThickness = 2.0f;

    ColorSpec subGridColor     = ColorSpec::fromArgb(QColor(0, 0, 0, 64));
    float     subGridThickness = 1.0f;

    int  gridSizeInStud    = 32;
    int  subDivisionNumber = 4;          // upstream clamps min 2

    bool displayGrid       = true;
    bool displaySubGrid    = true;
    bool displayCellIndex  = false;

    FontSpec cellIndexFont;
    ColorSpec cellIndexColor = ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));

    CellIndexType cellIndexColumnType = CellIndexType::Letters;
    CellIndexType cellIndexRowType    = CellIndexType::Numbers;
    QPoint        cellIndexCorner{ 0, 0 };
};

}
