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

#include "core/Map.h"
#include "core/Module.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "core/TextCell.h"
#include "rendering/MapText.h"
#include "rendering/ModuleLabels.h"
#include "rendering/UnknownPart.h"
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
rendering::TextWidthAt fixedWidth(const QString& text, double charWidth) {
    const auto n = static_cast<double>(text.size());
    return [n, charWidth](double fontPx) { return n * charWidth * fontPx; };
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
        const auto got = rendering::moduleLabelLayout(rectOf(c[QLatin1String("studs")].toObject()), name,
                                                      c[QLatin1String("labelPercent")].toDouble(),
                                                      fixedWidth(name, charWidth));
        const QJsonObject want = c[QLatin1String("expect")].toObject();
        const QRectF frame = rectOf(want[QLatin1String("frame")].toObject());
        EXPECT_EQ(got.frame, frame) << name.toStdString();
        const QJsonObject t = want[QLatin1String("text")].toObject();
        EXPECT_DOUBLE_EQ(got.textPos.x(), t[QLatin1String("x")].toDouble()) << name.toStdString();
        EXPECT_DOUBLE_EQ(got.textPos.y(), t[QLatin1String("y")].toDouble()) << name.toStdString();
        EXPECT_DOUBLE_EQ(got.rotation, t[QLatin1String("rotation")].toDouble()) << name.toStdString();
        EXPECT_DOUBLE_EQ(got.width, t[QLatin1String("width")].toDouble()) << name.toStdString();
        EXPECT_DOUBLE_EQ(got.fontPx, t[QLatin1String("fontPx")].toDouble()) << name.toStdString();
        EXPECT_EQ(got.bounds, rectOf(want[QLatin1String("bounds")].toObject())) << name.toStdString();
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
    const QJsonArray dash = style[QLatin1String("frameDash")].toArray();
    ASSERT_EQ(dash.size(), 2);
    EXPECT_EQ(rendering::kModuleFrameDash[0], dash[0].toDouble());
    EXPECT_EQ(rendering::kModuleFrameDash[1], dash[1].toDouble());
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

// A painted sheet's alpha is in its cells' colours only, not faded again.
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
