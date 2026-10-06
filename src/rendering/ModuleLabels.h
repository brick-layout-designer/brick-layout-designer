#pragma once

// Module frames and names, drawn as the web draws them
// (apps/web/src/editor/render/moduleLabels.ts + ModuleOverlay.tsx): a
// dashed frame 4 px outside the module's pieces, and the name in bold with
// a dark outline, 16 px outside the frame, on the side placeModuleNames
// picks: clear of parts and of other names, on the outside edge of a ring
// of modules, level above or below, turned along the left (reading bottom
// to top) or the right (top to bottom), inside only as a last resort. A
// name is never longer than the module's edge: it wraps to two lines, then
// shrinks, and only as a last resort is cut short with an ellipsis (the
// whole name shows on hover or select). Each module has its own default
// colour (moduleColours, from its id; neighbours differ), and can have its
// own chosen outline and name colours and hide its name.
// fixtures/render-parity/modules.json holds the numbers both apps are
// tested against.

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <QHash>

#include <functional>
#include <vector>

namespace bld::core { struct Module; }

namespace bld::rendering {

inline const QColor kModuleFrameColor(100, 180, 255, 204);  // rgba(100,180,255,0.8)
inline const QColor kModuleNameFill(100, 180, 255, 230);    // rgba(100,180,255,0.9)
inline const QColor kModuleNameStroke(0, 0, 0, 153);        // rgba(0,0,0,0.6)
// A chosen colour is drawn at the default look's opacity.
inline constexpr double kModuleFrameAlpha = 0.8;
inline constexpr double kModuleNameAlpha = 0.9;
// The pill behind a shortened name's whole name: rgba(0,0,0,0.75).
inline const QColor kModuleFullNameBackground(0, 0, 0, 191);
// The smallest a name gets before it is cut short, in scene px (2 studs).
inline constexpr double kModuleNameMinPx = 16.0;
// Lines of a two-line name are one font size apart.
inline constexpr double kModuleNameLineHeight = 1.0;
// Edit module: everything outside the edited module is dimmed, and its
// outline is drawn in the accent blue, 2 screen px, dashed 8/4, half a
// stud outside its parts (the web's ModuleEditDim.tsx).
inline const QColor kModuleEditDim = QColor::fromRgbF(15 / 255.0f, 23 / 255.0f, 42 / 255.0f, 0.5f);
inline const QColor kModuleEditOutline(37, 99, 235);
inline constexpr double kModuleEditOutlineWidth = 2.0;
inline constexpr double kModuleEditDash[2] = { 8.0, 4.0 };
inline constexpr double kModuleEditPadStuds = 0.5;
// The frame's dash and gap, in screen px.
inline constexpr double kModuleFrameDash[2] = { 6.0, 4.0 };
// A module with some parts on hidden sheets: short, far-apart dashes ("there's more you can't see").
inline constexpr double kModuleFramePartlyHiddenDash[2] = { 2.0, 6.0 };
// QGraphicsItem data role naming a module drawing: "frame" or "name".
inline constexpr int kModuleAnnotationRole = 3;
// QGraphicsItem data role holding the module id on its frame and name.
inline constexpr int kModuleIdRole = 4;
// QGraphicsItem data role on a module frame: true when some of its parts are on hidden sheets.
inline constexpr int kModulePartlyHiddenRole = 13;

// A bold line's width in px at a font size.
using NameWidthAt = std::function<double(const QString& text, double fontPx)>;

// The name's outline width: fontPx / 12, at least 2 px.
double moduleNameStrokePx(double fontPx);
// What a module's name says on hover: the whole name, and "(partly hidden)"
// when some of its parts are on a hidden sheet (the web's moduleHoverName).
// Empty when hover adds nothing.
QString moduleHoverName(const QString& name, bool partlyHidden, bool truncated);

// A name is never taller than this share of its module's short side.
inline constexpr double kModuleNameShortShare = 0.5;
// `percent`% of the frame's long axis, at most kModuleNameShortShare of its
// short axis, clamped to 16..400 px.
double moduleLabelFontPx(double frameW, double frameH, double percent);

// A name fitted to its side: one or two lines, never wider than the side.
struct ModuleNameFit {
    double fontPx = 0;
    QStringList lines;
    bool truncated = false;  // cut short with an ellipsis
};
// The web's fitModuleName, step for step: one line; else two lines
// (broken between words, as evenly as it goes); else smaller (one line or
// two, whichever stays bigger) down to kModuleNameMinPx; else at that size
// as many words as fit on the first line and the rest, cut short, on the
// second.
ModuleNameFit fitModuleName(const QString& text, const NameWidthAt& widthAt, double fontPx, double side);

// How one placed module is drawn: its colours (the default look unless
// chosen) and whether its name shows.
struct ModuleLook {
    QColor frame = kModuleFrameColor;
    QColor nameFill = kModuleNameFill;
    bool showName = true;
};
// `defaultHex`: the module's own default colour (moduleColours); empty: the light blue.
ModuleLook moduleLook(const core::Module& m, const QString& defaultHex = {});

// Each module's own default colour comes from this palette (the web's
// MODULE_PALETTE): distinct hues, light enough to read inside the name's
// dark outline, apart from the map's default blue.
inline const QStringList kModulePalette{ QStringLiteral("#FFE066"), QStringLiteral("#FFA94D"), QStringLiteral("#FCC2D7"),
                                         QStringLiteral("#E599F7"), QStringLiteral("#8CE99A"), QStringLiteral("#C0EB75"),
                                         QStringLiteral("#66D9E8"), QStringLiteral("#63E6BE") };
// Modules this close (studs) count as neighbours, which get different colours.
inline constexpr double kModuleNeighbourStuds = 4.0;
// FNV-1a over the id's UTF-16 code units: the same number in both apps.
quint32 moduleIdHash(const QString& id);
// Every module's default colour (a palette hex) from its id, stepping on
// through the palette when a neighbour earlier in the list has it. `box`:
// its visible parts' bounds in studs (empty: none show; no neighbours).
struct ModuleColourInput {
    QString id;
    QRectF box;
};
QHash<QString, QString> moduleColours(const std::vector<ModuleColourInput>& modules);

struct ModuleLabelLayout {
    QRectF frame;     // scene px
    bool hasName = false;  // false when the module's name is hidden
    // The name's anchor: a name turned -90° reads bottom to top from the
    // bottom-left of its box, one turned 90° top to bottom from the top-right.
    QPointF textPos;
    double rotation = 0;
    double width = 0;   // the name's box, along the side it sits on
    double height = 0;  // the name's box, across it (its lines)
    double fontPx = 0;
    QStringList lines;
    bool truncated = false;
    QRectF bounds;  // frame and name together
    QString side;   // top, bottom, left, right or inside; empty when hidden
    QString slot;   // which candidate place ("top:0", "inside:1"): a live drag keeps it
};

// Where one module's frame and name go, from its pieces' bounds in studs,
// on its own (no other modules or parts about).
ModuleLabelLayout moduleLabelLayout(const QRectF& studs, const QString& name, double labelPercent,
                                    const NameWidthAt& widthAt, bool showName = true);

// Places every module's frame and name together, as the web's
// placeModuleNames, step by step: each name in turn takes the lowest-
// scoring candidate place round its module (or inside it), keeping clear of
// `partsPx` (scene px) and the names placed before it, and on a ring of
// modules (nothing at the layout's middle) the outside edge.
struct ModuleNameInput {
    QString id;
    QString name;
    QRectF studs;  // its visible parts' bounds
    bool showName = true;
};
// `keep`: module id → the slot to keep (while parts are dragged, names stay on their side).
std::vector<ModuleLabelLayout> placeModuleNames(const std::vector<ModuleNameInput>& modules,
                                                const std::vector<QRectF>& partsPx, double labelPercent,
                                                const NameWidthAt& widthAt,
                                                const QHash<QString, QString>& keep = {});

// The pill with the whole name over a shortened one, in the name's own
// turned frame (x along the side, y across it): one line at the name's
// size, centred on the name's box, with a quarter-font margin.
struct ModuleFullNamePill {
    QRectF box;
    QPointF textTopLeft;
};
ModuleFullNamePill moduleFullNamePill(const ModuleLabelLayout& at, const QString& name, const NameWidthAt& widthAt);

}  // namespace bld::rendering
