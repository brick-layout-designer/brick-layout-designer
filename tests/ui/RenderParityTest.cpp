// The map must look the same in the desktop and on the web. Both apps draw
// fixtures/render-parity/parity.bld-layout's saved views at the same size
// (Export all views, Small), and the web's e2e renderParity.spec.ts draws
// the same views from its own copy of the fixture.
//
// Two checks:
// - Module frames and names (the redesign's look, the web is the
//   reference) follow fixtures/render-parity/modules.json, a description of
//   what's drawn that the web's moduleLabels test reads too. It uses a fixed
//   text width rule, so it doesn't depend on the fonts a machine has.
// - BLD_ENABLE_RENDER_GOLDENS=1 compares each picture with the golden PNG
//   (fixtures/render-parity/desktop/<view>.png) within a small tolerance;
//   fonts differ between machines, so it is a local gate like
//   RenderGoldens. BLD_PARITY_OUT=<dir> writes the pictures there.

#include "ui/SavedViews.h"
#include "ui/SelectionStyle.h"

#include "core/Map.h"
#include "core/Module.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "core/TextCell.h"
#include "rendering/MapText.h"
#include "rendering/ModuleLabels.h"
#include "rendering/UnknownPart.h"
#include "rendering/VenueLabels.h"
#include "core/Venue.h"
#include "rendering/SceneBuilder.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>

using namespace bld;

namespace {

const QString kDir = QStringLiteral(BLD_SOURCE_DIR "/fixtures/render-parity");

QJsonObject readJson(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return QJsonDocument::fromJson(f.readAll()).object();
}

QRectF rectOf(const QJsonObject& o) {
    return { o[QLatin1String("x")].toDouble(), o[QLatin1String("y")].toDouble(), o[QLatin1String("width")].toDouble(),
             o[QLatin1String("height")].toDouble() };
}

// The shared description's text width: `charWidth` × font px per character.
rendering::NameWidthAt fixedWidth(double charWidth) {
    return [charWidth](const QString& text, double fontPx) { return text.size() * charWidth * fontPx; };
}

QStringList stringsOf(const QJsonArray& a) {
    QStringList out;
    for (const QJsonValue& v : a) out << v.toString();
    return out;
}

}  // namespace

TEST(RenderParity, ModuleLabelLayoutsMatchTheSharedDescription) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/modules.json"));
    const double charWidth = spec[QLatin1String("charWidth")].toDouble();
    const QJsonArray cases = spec[QLatin1String("cases")].toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QString name = c[QLatin1String("name")].toString();
        const bool showName = c[QLatin1String("module")].toObject().value(QLatin1String("showName")).toBool(true);
        const auto got = rendering::moduleLabelLayout(rectOf(c[QLatin1String("studs")].toObject()), name,
                                                      c[QLatin1String("labelPercent")].toDouble(), fixedWidth(charWidth),
                                                      showName);
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        const std::string what = name.toStdString();
        EXPECT_EQ(got.frame, rectOf(want[QLatin1String("frame")].toObject())) << what;
        EXPECT_EQ(got.bounds, rectOf(want[QLatin1String("bounds")].toObject())) << what;
        EXPECT_EQ(got.hasName, want.contains(QLatin1String("text"))) << what;
        if (!want.contains(QLatin1String("text"))) continue;
        const QJsonObject t = want[QLatin1String("text")].toObject();
        EXPECT_DOUBLE_EQ(got.textPos.x(), t[QLatin1String("x")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.textPos.y(), t[QLatin1String("y")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.rotation, t[QLatin1String("rotation")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.width, t[QLatin1String("width")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.height, t[QLatin1String("height")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.fontPx, t[QLatin1String("fontPx")].toDouble()) << what;
        EXPECT_EQ(got.lines, stringsOf(t[QLatin1String("lines")].toArray())) << what;
        EXPECT_EQ(got.truncated, t[QLatin1String("truncated")].toBool()) << what;
    }
}

// Names placed together (the Fordyce ring and friends): the same sides,
// turns, sizes and places as the web's placeModuleNames.
TEST(RenderParity, ModuleNamePlacementMatchesTheSharedDescription) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/modules.json"));
    const auto width = fixedWidth(spec[QLatin1String("charWidth")].toDouble());
    const QJsonArray cases = spec[QLatin1String("placement")].toArray();
    ASSERT_GE(cases.size(), 4);
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const std::string what = c[QLatin1String("name")].toString().toStdString();
        std::vector<rendering::ModuleNameInput> in;
        for (const QJsonValue& m : c[QLatin1String("modules")].toArray()) {
            const QJsonObject o = m.toObject();
            const QJsonObject r = o[QLatin1String("studs")].toObject();
            in.push_back({ o[QLatin1String("id")].toString(), o[QLatin1String("name")].toString(),
                           QRectF(r[QLatin1String("x")].toDouble(), r[QLatin1String("y")].toDouble(),
                                  r[QLatin1String("w")].toDouble(), r[QLatin1String("h")].toDouble()) });
        }
        std::vector<QRectF> parts;
        for (const QJsonValue& p : c[QLatin1String("parts")].toArray()) {
            const QJsonObject r = p.toObject();
            parts.emplace_back(r[QLatin1String("x")].toDouble() * 8, r[QLatin1String("y")].toDouble() * 8,
                               r[QLatin1String("w")].toDouble() * 8, r[QLatin1String("h")].toDouble() * 8);
        }
        const auto got = rendering::placeModuleNames(in, parts, c[QLatin1String("labelPercent")].toDouble(), width);
        const QJsonArray want = c[QLatin1String("expect")].toArray();
        ASSERT_EQ(got.size(), static_cast<std::size_t>(want.size())) << what;
        for (qsizetype i = 0; i < want.size(); ++i) {
            const QJsonObject e = want[i].toObject();
            const QJsonObject t = e[QLatin1String("text")].toObject();
            const std::string who = what + " / " + e[QLatin1String("id")].toString().toStdString();
            const auto& g = got[static_cast<std::size_t>(i)];
            EXPECT_EQ(g.side, e[QLatin1String("side")].toString()) << who;
            EXPECT_DOUBLE_EQ(g.rotation, t[QLatin1String("rotation")].toDouble()) << who;
            EXPECT_NEAR(g.textPos.x(), t[QLatin1String("x")].toDouble(), 0.01) << who;
            EXPECT_NEAR(g.textPos.y(), t[QLatin1String("y")].toDouble(), 0.01) << who;
            EXPECT_DOUBLE_EQ(g.width, t[QLatin1String("width")].toDouble()) << who;
            EXPECT_DOUBLE_EQ(g.height, t[QLatin1String("height")].toDouble()) << who;
            EXPECT_DOUBLE_EQ(g.fontPx, t[QLatin1String("fontPx")].toDouble()) << who;
            EXPECT_EQ(g.lines, stringsOf(t[QLatin1String("lines")].toArray())) << who;
        }
    }
}

// While a module is dragged its name keeps its place (keep), as on the web.
TEST(RenderParity, ADraggedModuleKeepsItsNamesPlace) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/modules.json"));
    const auto width = fixedWidth(spec[QLatin1String("charWidth")].toDouble());
    const std::vector<QRectF> parts{ QRectF(0, 0, 1600, 800), QRectF(-400, -1200, 2400, 1120) };
    std::vector<rendering::ModuleNameInput> in{ { QStringLiteral("yard"), QStringLiteral("Yard"), QRectF(0, 0, 200, 100) } };
    const auto settled = rendering::placeModuleNames(in, parts, 35, width).front();
    EXPECT_EQ(settled.slot, QStringLiteral("bottom:0"));
    in.front().studs.translate(0, -400);
    EXPECT_EQ(rendering::placeModuleNames(in, parts, 35, width).front().slot, QStringLiteral("top:0"));
    const auto kept = rendering::placeModuleNames(in, parts, 35, width, { { QStringLiteral("yard"), settled.slot } }).front();
    EXPECT_EQ(kept.slot, QStringLiteral("bottom:0"));
    EXPECT_NEAR(kept.textPos.y() - settled.textPos.y(), -400 * 8, 1e-6);
}

// Each module's own default color: from its id, neighbours apart.
TEST(RenderParity, ModuleDefaultColorsMatchTheSharedDescription) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/modules.json"))[QLatin1String("colors")].toObject();
    EXPECT_EQ(rendering::kModulePalette, stringsOf(spec[QLatin1String("palette")].toArray()));
    EXPECT_DOUBLE_EQ(rendering::kModuleNeighbourStuds, spec[QLatin1String("neighbourStuds")].toDouble());
    for (const QJsonValue& v : spec[QLatin1String("hashes")].toArray()) {
        const QJsonObject h = v.toObject();
        EXPECT_EQ(rendering::moduleIdHash(h[QLatin1String("id")].toString()),
                  static_cast<quint32>(h[QLatin1String("hash")].toDouble()))
            << h[QLatin1String("id")].toString().toStdString();
    }
    for (const QJsonValue& v : spec[QLatin1String("cases")].toArray()) {
        const QJsonObject c = v.toObject();
        std::vector<rendering::ModuleColorInput> in;
        for (const QJsonValue& m : c[QLatin1String("modules")].toArray()) {
            const QJsonObject o = m.toObject();
            const QJsonObject r = o[QLatin1String("box")].toObject();
            in.push_back({ o[QLatin1String("id")].toString(),
                           r.isEmpty() ? QRectF() : QRectF(r[QLatin1String("x")].toDouble(), r[QLatin1String("y")].toDouble(),
                                                           r[QLatin1String("w")].toDouble(), r[QLatin1String("h")].toDouble()) });
        }
        const auto got = rendering::moduleColors(in);
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        for (auto it = want.begin(); it != want.end(); ++it)
            EXPECT_EQ(got.value(it.key()), it.value().toString()) << c[QLatin1String("name")].toString().toStdString() << " / " << it.key().toStdString();
    }
}

// The palette is readable: light inside the dark outline, apart from the
// map's default blue and from each other.
TEST(RenderParity, ModuleDefaultColorsAreReadable) {
    const auto lum = [](const QColor& c) {
        const auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
    };
    const auto contrast = [&](const QColor& a, const QColor& b) {
        const double x = lum(a), y = lum(b);
        return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
    };
    const auto gap = [](double a, double b) { return std::min(std::abs(a - b), 360 - std::abs(a - b)); };
    const QColor blue(QStringLiteral("#6495ED"));
    for (const QString& hex : rendering::kModulePalette) {
        const QColor c(hex);
        EXPECT_GE(contrast(c, Qt::black), 10.0) << hex.toStdString();
        EXPECT_GE(contrast(c, blue), 1.4) << hex.toStdString();
        EXPECT_GE(gap(c.hsvHueF() * 360, blue.hsvHueF() * 360), 30.0) << hex.toStdString();
    }
}

// Wrap, then shrink, then cut short: the same answers as the web's fitModuleName.
TEST(RenderParity, ModuleNameFitsMatchTheSharedDescription) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/modules.json"));
    const auto width = fixedWidth(spec[QLatin1String("charWidth")].toDouble());
    EXPECT_DOUBLE_EQ(rendering::kModuleNameMinPx, spec[QLatin1String("minFontPx")].toDouble());
    const QJsonArray cases = spec[QLatin1String("fit")].toArray();
    ASSERT_GE(cases.size(), 8);
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QString text = c[QLatin1String("text")].toString();
        const double side = c[QLatin1String("side")].toDouble();
        const auto got = rendering::fitModuleName(text, width, c[QLatin1String("fontPx")].toDouble(), side);
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        const std::string what = text.toStdString();
        EXPECT_DOUBLE_EQ(got.fontPx, want[QLatin1String("fontPx")].toDouble()) << what;
        EXPECT_EQ(got.lines, stringsOf(want[QLatin1String("lines")].toArray())) << what;
        EXPECT_EQ(got.truncated, want[QLatin1String("truncated")].toBool()) << what;
        // Never longer than the side.
        for (const QString& line : got.lines) EXPECT_LE(width(line, got.fontPx), side) << what;
    }
}

// Real widths aren't quite in proportion to the size: the shrunk size is
// checked and made smaller until it fits (as the web's test).
TEST(RenderParity, ModuleNameShrinkChecksTheSizeReallyFits) {
    const rendering::NameWidthAt padded = [](const QString& t, double px) { return t.size() * px * 0.5 + 30; };
    const QString name = QStringLiteral("Straightaway");
    EXPECT_DOUBLE_EQ(rendering::fitModuleName(name, padded, 100, 470).fontPx, 73.0);
    const auto big = rendering::fitModuleName(name, padded, 100, 400);
    EXPECT_LE(padded(name, big.fontPx), 400.0);
    EXPECT_GT(padded(name, big.fontPx + 1), 400.0);
}

TEST(RenderParity, ModuleLooksMatchTheSharedDescription) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/modules.json"));
    const auto rgba = [](const QColor& c) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'g', 2);
    };
    const QJsonArray cases = spec[QLatin1String("looks")].toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QJsonObject m = c[QLatin1String("module")].toObject();
        core::Module mod;
        mod.outlineColor = m[QLatin1String("outlineColor")].toString();
        mod.nameColor = m[QLatin1String("nameColor")].toString();
        mod.sameColor = m[QLatin1String("sameColor")].toBool(true);
        mod.showName = m[QLatin1String("showName")].toBool(true);
        const auto look = rendering::moduleLook(mod);
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        EXPECT_EQ(rgba(look.frame), want[QLatin1String("frameStroke")].toString());
        EXPECT_EQ(rgba(look.nameFill), want[QLatin1String("nameFill")].toString());
        EXPECT_EQ(look.showName, want[QLatin1String("showName")].toBool());
    }
}

TEST(RenderParity, ModuleStyleMatchesTheSharedDescription) {
    const QJsonObject style = readJson(kDir + QStringLiteral("/modules.json"))[QLatin1String("style")].toObject();
    const auto rgba = [](const QColor& c) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'g', 2);
    };
    EXPECT_EQ(rgba(rendering::kModuleFrameColor), style[QLatin1String("frameStroke")].toString());
    EXPECT_EQ(rgba(rendering::kModuleNameFill), style[QLatin1String("nameFill")].toString());
    EXPECT_EQ(rgba(rendering::kModuleNameStroke), style[QLatin1String("nameStroke")].toString());
    EXPECT_EQ(rgba(rendering::kModuleFullNameBackground), style[QLatin1String("fullNameBackground")].toString());
    EXPECT_DOUBLE_EQ(rendering::kModuleNameLineHeight, style[QLatin1String("nameLineHeight")].toDouble());
    EXPECT_DOUBLE_EQ(rendering::kModuleFrameAlpha, style[QLatin1String("customFrameAlpha")].toDouble());
    EXPECT_DOUBLE_EQ(rendering::kModuleNameAlpha, style[QLatin1String("customNameAlpha")].toDouble());
    const QJsonArray dash = style[QLatin1String("frameDash")].toArray();
    ASSERT_EQ(dash.size(), 2);
    EXPECT_EQ(rendering::kModuleFrameDash[0], dash[0].toDouble());
    EXPECT_EQ(rendering::kModuleFrameDash[1], dash[1].toDouble());
    const QJsonArray hiddenDash = style[QLatin1String("partlyHiddenFrameDash")].toArray();
    ASSERT_EQ(hiddenDash.size(), 2);
    EXPECT_EQ(rendering::kModuleFramePartlyHiddenDash[0], hiddenDash[0].toDouble());
    EXPECT_EQ(rendering::kModuleFramePartlyHiddenDash[1], hiddenDash[1].toDouble());
    // The name's outline: fontPx / 12, at least 2 px.
    EXPECT_DOUBLE_EQ(rendering::moduleNameStrokePx(12), 2.0);
    EXPECT_DOUBLE_EQ(rendering::moduleNameStrokePx(120), 10.0);
}

TEST(RenderParity, TextCellLayoutsMatchTheSharedDescription) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/text.json"));
    const double charWidth = spec[QLatin1String("charWidth")].toDouble();
    EXPECT_DOUBLE_EQ(rendering::kMapLineHeight, spec[QLatin1String("lineHeight")].toDouble());
    EXPECT_EQ(rendering::mapFontFamily(), spec[QLatin1String("fontFamily")].toString());
    const QJsonArray cases = spec[QLatin1String("cases")].toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        core::TextCell cell;
        cell.text = c[QLatin1String("text")].toString();
        cell.displayArea = rectOf(c[QLatin1String("displayArea")].toObject());
        cell.orientation = static_cast<float>(c[QLatin1String("orientation")].toDouble());
        const QString align = c[QLatin1String("textAlignment")].toString();
        cell.alignment = align == QLatin1String("Near")  ? core::TextAlignment::Near
                         : align == QLatin1String("Far") ? core::TextAlignment::Far
                                                         : core::TextAlignment::Center;
        const auto got = rendering::textCellLayout(
            cell, [charWidth](const QString& line, double px) { return line.size() * charWidth * px; });
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        const std::string what = cell.text.toStdString();
        EXPECT_DOUBLE_EQ(got.fontPx, want[QLatin1String("fontPx")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.width, want[QLatin1String("width")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.height, want[QLatin1String("height")].toDouble()) << what;
        const QJsonObject centre = want[QLatin1String("centre")].toObject();
        EXPECT_DOUBLE_EQ(got.centre.x(), centre[QLatin1String("x")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.centre.y(), centre[QLatin1String("y")].toDouble()) << what;
        EXPECT_DOUBLE_EQ(got.rotation, want[QLatin1String("rotation")].toDouble()) << what;
        const QJsonArray lines = want[QLatin1String("lines")].toArray();
        ASSERT_EQ(got.lines.size(), static_cast<size_t>(lines.size())) << what;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QJsonObject l = lines[i].toObject();
            const auto& g = got.lines[static_cast<size_t>(i)];
            EXPECT_EQ(g.text, l[QLatin1String("text")].toString()) << what;
            EXPECT_DOUBLE_EQ(g.x, l[QLatin1String("x")].toDouble()) << what;
            EXPECT_DOUBLE_EQ(g.y, l[QLatin1String("y")].toDouble()) << what;
        }
    }
}

TEST(RenderParity, UnknownPartsMatchTheSharedDescription) {
    const QJsonArray cases = readJson(kDir + QStringLiteral("/parts.json"))[QLatin1String("cases")].toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QString part = c[QLatin1String("partNumber")].toString();
        const auto got = rendering::unknownPartLook(part, c[QLatin1String("widthStuds")].toDouble(),
                                                    c[QLatin1String("heightStuds")].toDouble());
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        EXPECT_DOUBLE_EQ(got.width, want[QLatin1String("width")].toDouble()) << part.toStdString();
        EXPECT_DOUBLE_EQ(got.height, want[QLatin1String("height")].toDouble()) << part.toStdString();
        EXPECT_DOUBLE_EQ(got.penPx, want[QLatin1String("penPx")].toDouble()) << part.toStdString();
        EXPECT_DOUBLE_EQ(got.fontPx, want[QLatin1String("fontPx")].toDouble()) << part.toStdString();
    }
}

TEST(RenderParity, AreaCellsMatchTheSharedDescription) {
    const QJsonArray cases = readJson(kDir + QStringLiteral("/areas.json"))[QLatin1String("cases")].toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QString hex = c[QLatin1String("color")].toString();
        const QColor cell = QColor::fromRgba(hex.toUInt(nullptr, 16));
        const QColor got = rendering::areaCellColor(cell, c[QLatin1String("transparency")].toInt());
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        EXPECT_EQ(got.red(), want[QLatin1String("r")].toInt()) << hex.toStdString();
        EXPECT_EQ(got.green(), want[QLatin1String("g")].toInt()) << hex.toStdString();
        EXPECT_EQ(got.blue(), want[QLatin1String("b")].toInt()) << hex.toStdString();
        EXPECT_EQ(got.alpha(), want[QLatin1String("a")].toInt()) << hex.toStdString();
    }
}

// A painted sheet's alpha is in its cells' colors only, not faded again.
TEST(RenderParity, SceneFadesAreaCellsOnce) {
    QTemporaryDir dir;
    auto read = import::readLayoutFile(kDir + QStringLiteral("/rulers-areas.bld-layout"), dir.path());
    ASSERT_TRUE(read.ok()) << read.error.toStdString();
    parts::PartsLibrary parts;
    QGraphicsScene scene;
    rendering::SceneBuilder builder(scene, parts);
    builder.build(*read.map);
    int cells = 0;
    for (QGraphicsItem* it : scene.items()) {
        auto* r = dynamic_cast<QGraphicsRectItem*>(it);
        if (!r || r->brush().style() != Qt::SolidPattern || r->brush().color().alpha() != 153) continue;
        ++cells;  // the Zones sheet is 60%: alpha 153
        EXPECT_DOUBLE_EQ(r->opacity(), 1.0);
    }
    EXPECT_EQ(cells, 3);
}

namespace {
// "rgb(r,g,b)" / "rgba(r,g,b,a)" as a color, alpha rounded to 0..255.
QColor css(const QJsonValue& v) {
    const QString t = v.toString();
    const QStringList n = t.mid(t.indexOf(QLatin1Char('(')) + 1).chopped(1).split(QLatin1Char(','));
    QColor c(n.value(0).toInt(), n.value(1).toInt(), n.value(2).toInt());
    if (n.size() == 4) c.setAlpha(static_cast<int>(std::lround(n[3].toDouble() * 255)));
    return c;
}
}  // namespace

TEST(RenderParity, SelectionMatchesTheSharedDescription) {
    using namespace ui::selection;
    const QJsonObject spec = readJson(kDir + QStringLiteral("/selection.json"));
    const QJsonObject part = spec[QLatin1String("part")].toObject();
    EXPECT_EQ(kPartPadPx, part[QLatin1String("padPx")].toDouble());
    EXPECT_EQ(kPartOuter, css(part[QLatin1String("outer")]));
    EXPECT_EQ(kPartOuterWidth, part[QLatin1String("outerWidth")].toDouble());
    EXPECT_EQ(kPartInnerWidth, part[QLatin1String("innerWidth")].toDouble());
    EXPECT_EQ(kPartTint.name().toUpper(), part[QLatin1String("tint")].toString());
    EXPECT_EQ(kPartFillAlpha, part[QLatin1String("fillAlpha")].toInt());
    EXPECT_EQ(kSnapStroke, css(part[QLatin1String("snapStroke")]));
    EXPECT_EQ(kSnapFill, css(part[QLatin1String("snapFill")]));
    const QJsonObject text = spec[QLatin1String("text")].toObject();
    EXPECT_EQ(kTextGlow.name(), text[QLatin1String("glow")].toString());
    EXPECT_EQ(kTextGlowBlur, text[QLatin1String("blur")].toDouble());
    const QJsonObject ruler = spec[QLatin1String("ruler")].toObject();
    EXPECT_EQ(kRulerHalo, css(ruler[QLatin1String("halo")]));
    for (const QJsonValue& v : ruler[QLatin1String("haloWidth")].toArray())
        EXPECT_EQ(rulerHaloWidth(v[QLatin1String("thickness")].toDouble()), v[QLatin1String("width")].toDouble());
    const QJsonObject handle = ruler[QLatin1String("handle")].toObject();
    EXPECT_EQ(kHandleRadius, handle[QLatin1String("radius")].toDouble());
    EXPECT_EQ(kHandleFill, css(handle[QLatin1String("fill")]));
    EXPECT_EQ(kHandleStroke, css(handle[QLatin1String("stroke")]));
    EXPECT_EQ(kHandleStrokeWidth, handle[QLatin1String("strokeWidth")].toDouble());
    const QJsonObject snap = spec[QLatin1String("snap")].toObject();
    EXPECT_EQ(snapmarks::kRing, css(snap[QLatin1String("ring")]));
    EXPECT_EQ(snapmarks::kRingRadius, snap[QLatin1String("ringRadius")].toDouble());
    EXPECT_EQ(snapmarks::kRingWidth, snap[QLatin1String("ringWidth")].toDouble());
    EXPECT_EQ(snapmarks::kRingFill, css(snap[QLatin1String("ringFill")]));
    EXPECT_EQ(snapmarks::kHalo, css(snap[QLatin1String("halo")]));
    EXPECT_EQ(snapmarks::kHaloWidth, snap[QLatin1String("haloWidth")].toDouble());
    EXPECT_EQ(snapmarks::kDot, css(snap[QLatin1String("dot")]));
    EXPECT_EQ(snapmarks::kDotRadius, snap[QLatin1String("dotRadius")].toDouble());
    EXPECT_EQ(snapmarks::kDotHaloWidth, snap[QLatin1String("dotHaloWidth")].toDouble());
}

// The drawn scene: one dashed frame and one outlined name per module, no
// white box behind it.
TEST(RenderParity, SceneDrawsTheWebsModuleLook) {
    QSettings().remove(QStringLiteral("view/moduleNames"));
    QTemporaryDir dir;
    auto read = import::readLayoutFile(kDir + QStringLiteral("/parity.bld-layout"), dir.path());
    ASSERT_TRUE(read.ok()) << read.error.toStdString();
    parts::PartsLibrary parts;
    QGraphicsScene scene;
    rendering::SceneBuilder builder(scene, parts);
    builder.build(*read.map);
    int frames = 0, names = 0;
    for (QGraphicsItem* it : scene.items()) {
        const QString kind = it->data(rendering::kModuleAnnotationRole).toString();
        if (kind == QLatin1String("frame")) ++frames;
        if (kind == QLatin1String("name")) ++names;
        EXPECT_NE(kind, QLatin1String("box")) << "no box behind a module's name";
    }
    EXPECT_EQ(frames, static_cast<int>(read.map->sidecar.modules.size()));
    EXPECT_EQ(names, static_cast<int>(read.map->sidecar.modules.size()));
}

// Each saved view's picture, as Export all views (Small) makes it, for
// every parity layout (parity: modules, a see-through sheet, text, the
// room; rulers-areas: rulers and painted areas).
TEST(RenderParity, PicturesMatchTheGoldens) {
    QSettings().remove(QStringLiteral("view/moduleNames"));
    const QString out = qEnvironmentVariable("BLD_PARITY_OUT");
    const bool compare = qEnvironmentVariableIntValue("BLD_ENABLE_RENDER_GOLDENS") == 1;
    parts::PartsLibrary parts;
    parts.addSearchPath(QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
    parts.scan();
    for (const QString& stem : { QStringLiteral("parity"), QStringLiteral("rulers-areas") }) {
        QTemporaryDir dir;
        auto read = import::readLayoutFile(kDir + QLatin1Char('/') + stem + QStringLiteral(".bld-layout"), dir.path());
        ASSERT_TRUE(read.ok()) << read.error.toStdString();
        ui::views::PictureRenderer renderer(*read.map, parts);
        ASSERT_FALSE(read.map->sidecar.views.empty());
        for (const auto& view : read.map->sidecar.views) {
            const auto spec = ui::views::viewPicture(view, *read.map, &renderer.builder());
            ASSERT_TRUE(spec.has_value()) << view.name.toStdString();
            const QSize size = ui::views::pictureSize(spec->region, ui::views::scaleForSide(spec->region, 1280));
            const QImage img = renderer.render(*spec, size).convertToFormat(QImage::Format_ARGB32);
            ASSERT_EQ(img.size(), size);
            if (!out.isEmpty()) img.save(QDir(out).filePath(view.name + QStringLiteral(".png")));
            if (!compare) continue;
            const QImage golden(kDir + QStringLiteral("/desktop/") + view.name + QStringLiteral(".png"));
            ASSERT_FALSE(golden.isNull()) << view.name.toStdString();
            const QImage want = golden.convertToFormat(QImage::Format_ARGB32);
            ASSERT_EQ(want.size(), img.size());
            long long off = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb a = img.pixel(x, y), b = want.pixel(x, y);
                    if (std::abs(qRed(a) - qRed(b)) > 8 || std::abs(qGreen(a) - qGreen(b)) > 8 ||
                        std::abs(qBlue(a) - qBlue(b)) > 8)
                        ++off;
                }
            EXPECT_LE(static_cast<double>(off) / (img.width() * img.height()), 0.002) << view.name.toStdString();
        }
    }
}

// Venue wall labels: the cases the web's renderParity test checks too.
TEST(RenderParity, VenueWallLabelsMatchTheSharedDescription) {
    const QJsonObject spec = readJson(kDir + QStringLiteral("/venue-labels.json"));
    const double cw = spec[QLatin1String("charWidth")].toDouble();
    const QJsonObject pill = spec[QLatin1String("pill")].toObject();
    EXPECT_DOUBLE_EQ(rendering::VenueLabelPill::padX, pill[QLatin1String("padX")].toDouble());
    EXPECT_DOUBLE_EQ(rendering::VenueLabelPill::padY, pill[QLatin1String("padY")].toDouble());
    EXPECT_DOUBLE_EQ(rendering::VenueLabelPill::gap, pill[QLatin1String("gap")].toDouble());
    EXPECT_DOUBLE_EQ(rendering::VenueLabelPill::handleGap, pill[QLatin1String("handleGap")].toDouble());
    EXPECT_DOUBLE_EQ(rendering::VenueLabelPill::radius, pill[QLatin1String("radius")].toDouble());
    const auto rgba = [](const QColor& c) {
        return c.alpha() == 255 ? QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue())
                                : QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'g', 2);
    };
    for (const bool dark : { false, true }) {
        const QJsonObject t = spec[QLatin1String("theme")].toObject()[dark ? QLatin1String("dark") : QLatin1String("light")].toObject();
        const auto c = rendering::venueLabelColors(dark);
        EXPECT_EQ(rgba(c.fill), t[QLatin1String("fill")].toString());
        EXPECT_EQ(rgba(c.border), t[QLatin1String("border")].toString());
        EXPECT_EQ(rgba(c.text), t[QLatin1String("text")].toString());
    }
    EXPECT_EQ(rgba(rendering::kVenueGridFade), spec[QLatin1String("gridFade")].toString());
    for (const QJsonValue& v : spec[QLatin1String("cases")].toArray()) {
        const QJsonObject c = v.toObject();
        QVector<core::VenueEdge> edges;
        for (const QJsonValue& ev : c[QLatin1String("edges")].toArray()) {
            const QJsonObject e = ev.toObject();
            core::VenueEdge edge;
            edge.label = e[QLatin1String("label")].toString();
            edge.estimated = e[QLatin1String("estimated")].toBool();
            for (const QJsonValue& p : e[QLatin1String("poly")].toArray())
                edge.polyline << QPointF(p[QLatin1String("x")].toDouble(), p[QLatin1String("y")].toDouble());
            edges.push_back(edge);
        }
        rendering::VenueLabelOptions opts;
        opts.fontPx = c[QLatin1String("fontPx")].toDouble();
        opts.measure = [cw](const QString& t, double f) { return t.size() * cw * f; };
        if (c.contains(QLatin1String("selectedEdge"))) opts.selectedEdge = c[QLatin1String("selectedEdge")].toInt();
        for (const QJsonValue& h : c[QLatin1String("handles")].toArray())
            opts.handles << QPointF(h[QLatin1String("x")].toDouble(), h[QLatin1String("y")].toDouble());
        opts.handleHalfPx = c[QLatin1String("handleHalfPx")].toDouble();
        const auto got = rendering::venueEdgeLabels(edges, opts);
        const QJsonArray want = c[QLatin1String("expect")].toArray();
        const std::string what = c[QLatin1String("name")].toString().toStdString();
        ASSERT_EQ(got.size(), static_cast<size_t>(want.size())) << what;
        for (size_t i = 0; i < got.size(); ++i) {
            const QJsonObject w = want[static_cast<int>(i)].toObject();
            EXPECT_EQ(got[i].edge, w[QLatin1String("edge")].toInt()) << what;
            EXPECT_EQ(got[i].text, w[QLatin1String("text")].toString()) << what;
            EXPECT_EQ(got[i].full, w[QLatin1String("full")].toString()) << what;
            EXPECT_EQ(got[i].shortened, w[QLatin1String("shortened")].toBool()) << what;
            EXPECT_NEAR(got[i].centre.x(), w[QLatin1String("x")].toDouble(), 1e-4) << what;
            EXPECT_NEAR(got[i].centre.y(), w[QLatin1String("y")].toDouble(), 1e-4) << what;
            EXPECT_NEAR(got[i].angle, w[QLatin1String("angle")].toDouble(), 1e-6) << what;
            EXPECT_NEAR(got[i].width, w[QLatin1String("width")].toDouble(), 1e-4) << what;
            EXPECT_NEAR(got[i].height, w[QLatin1String("height")].toDouble(), 1e-4) << what;
        }
    }
    // Turned pills: touching isn't overlapping.
    const auto box = [](double x, double y, double angle, double w, double h) {
        rendering::VenueLabel l;
        l.centre = QPointF(x, y);
        l.angle = angle;
        l.width = w;
        l.height = h;
        return l;
    };
    EXPECT_TRUE(rendering::pillsOverlap(box(0, 0, 0, 10, 2), box(9, 0, 0, 10, 2)));
    EXPECT_FALSE(rendering::pillsOverlap(box(0, 0, 0, 10, 2), box(10, 0, 0, 10, 2)));
    EXPECT_TRUE(rendering::pillsOverlap(box(0, 0, 0, 10, 2), box(0, 4, 90, 6, 2)));
    EXPECT_FALSE(rendering::pillsOverlap(box(0, 0, 0, 10, 2), box(0, 5.5, 90, 6, 2)));
    // Upright: left to right, bottom to top on a vertical wall.
    EXPECT_DOUBLE_EQ(rendering::uprightAngle(0, 1), -90.0);
    EXPECT_DOUBLE_EQ(rendering::uprightAngle(0, -1), -90.0);
    EXPECT_NEAR(rendering::uprightAngle(-1, -1), 45.0, 1e-9);
}
