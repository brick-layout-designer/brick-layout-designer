#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace bld::ui::theme {

namespace {

// The icon on a 24 x 24 grid, like the mockup's SVGs.
QPainterPath shape(const QString& name) {
    QPainterPath p;
    if (name == QLatin1String("select")) {
        p.moveTo(5, 3); p.lineTo(19, 11); p.lineTo(13, 13); p.lineTo(11, 19); p.closeSubpath();
    } else if (name == QLatin1String("measure")) {
        p.moveTo(3, 17); p.lineTo(17, 3); p.lineTo(21, 7); p.lineTo(7, 21); p.closeSubpath();
        for (int i = 0; i < 3; ++i) {
            const double x = 8 + i * 3.5, y = 12 - i * 3.5;
            p.moveTo(x, y); p.lineTo(x + 2, y + 2);
        }
    } else if (name == QLatin1String("circle")) {
        p.addEllipse(QPointF(12, 12), 8, 8);
        p.moveTo(12, 12); p.lineTo(20, 12);
    } else if (name == QLatin1String("paint")) {
        p.addRoundedRect(QRectF(4, 4, 14, 6), 1.5, 1.5);
        p.moveTo(18, 7); p.lineTo(20, 7); p.lineTo(20, 12); p.lineTo(12, 12); p.lineTo(12, 15);
        p.addRect(QRectF(10.5, 15, 3, 6));
    } else if (name == QLatin1String("erase")) {
        p.moveTo(8, 20); p.lineTo(3.5, 15.5); p.lineTo(14, 5); p.lineTo(20.5, 11.5); p.lineTo(12, 20);
        p.closeSubpath();
        p.moveTo(9, 10); p.lineTo(15.5, 16.5);
        p.moveTo(12, 20); p.lineTo(21, 20);
    } else if (name == QLatin1String("undo") || name == QLatin1String("redo")) {
        p.moveTo(9, 14); p.lineTo(4, 9); p.lineTo(9, 4);
        p.moveTo(4, 9); p.lineTo(15, 9);
        p.arcTo(QRectF(10, 9, 10, 10), 90, -180);
        p.lineTo(12, 19);
        if (name == QLatin1String("redo")) p = QTransform(-1, 0, 0, 1, 24, 0).map(p);
    } else if (name == QLatin1String("new")) {
        p.moveTo(6, 3); p.lineTo(14, 3); p.lineTo(19, 8); p.lineTo(19, 21); p.lineTo(6, 21); p.closeSubpath();
        p.moveTo(14, 3); p.lineTo(14, 8); p.lineTo(19, 8);
        p.moveTo(12.5, 11); p.lineTo(12.5, 18); p.moveTo(9, 14.5); p.lineTo(16, 14.5);
    } else if (name == QLatin1String("open")) {
        p.moveTo(3, 6); p.lineTo(9, 6); p.lineTo(11, 8); p.lineTo(21, 8); p.lineTo(21, 19); p.lineTo(3, 19);
        p.closeSubpath();
        p.moveTo(3, 11); p.lineTo(21, 11);
    } else if (name == QLatin1String("save")) {
        p.moveTo(4, 4); p.lineTo(17, 4); p.lineTo(20, 7); p.lineTo(20, 20); p.lineTo(4, 20); p.closeSubpath();
        p.addRect(QRectF(8, 4, 7, 5));
        p.addRect(QRectF(7, 13, 10, 7));
    } else if (name == QLatin1String("delete")) {
        p.moveTo(4, 7); p.lineTo(20, 7);
        p.moveTo(9, 7); p.lineTo(9, 4); p.lineTo(15, 4); p.lineTo(15, 7);
        p.moveTo(6, 7); p.lineTo(7, 20); p.lineTo(17, 20); p.lineTo(18, 7);
        p.moveTo(10, 11); p.lineTo(10, 16); p.moveTo(14, 11); p.lineTo(14, 16);
    } else if (name == QLatin1String("turnLeft") || name == QLatin1String("turnRight")) {
        p.arcMoveTo(QRectF(5, 5, 14, 14), 150);
        p.arcTo(QRectF(5, 5, 14, 14), 150, -270);
        p.moveTo(4, 5); p.lineTo(5.9, 8.5); p.lineTo(9.5, 7);
        if (name == QLatin1String("turnRight")) p = QTransform(-1, 0, 0, 1, 24, 0).map(p);
    } else if (name == QLatin1String("snap")) {
        p.addRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
        p.moveTo(9.5, 4); p.lineTo(9.5, 20); p.moveTo(14.5, 4); p.lineTo(14.5, 20);
        p.moveTo(4, 9.5); p.lineTo(20, 9.5); p.moveTo(4, 14.5); p.lineTo(20, 14.5);
    } else if (name == QLatin1String("angle")) {
        p.moveTo(18, 5); p.lineTo(5, 19); p.lineTo(20, 19);
        p.arcMoveTo(QRectF(-3, 11, 16, 16), 0);
        p.arcTo(QRectF(-3, 11, 16, 16), 0, 48);
    } else if (name == QLatin1String("picture")) {
        // The web's PictureIcon: a frame, the sun and a hill.
        p.addRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        p.addEllipse(QPointF(9, 10), 1.6, 1.6);
        p.moveTo(21, 16); p.lineTo(16, 11); p.lineTo(8, 19);
    } else if (name == QLatin1String("chevronDown")) {
        p.moveTo(6, 9); p.lineTo(12, 15); p.lineTo(18, 9);
    } else if (name == QLatin1String("chevronUp")) {
        p.moveTo(6, 15); p.lineTo(12, 9); p.lineTo(18, 15);
    }
    return p;
}

QPixmap draw(const QPainterPath& path, const QColor& colour, int px, qreal dpr) {
    QPixmap pm(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    g.scale(px / 24.0, px / 24.0);
    g.setPen(QPen(colour, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    g.setBrush(Qt::NoBrush);
    g.drawPath(path);
    return pm;
}

}  // namespace

QIcon lineIcon(const QString& name, const QPalette& palette) {
    const QPainterPath path = shape(name);
    const QColor muted = palette.color(QPalette::PlaceholderText);
    const QColor ink = palette.color(QPalette::WindowText);
    const QColor accent = palette.color(QPalette::Link);
    const QColor off = palette.color(QPalette::Mid);
    QIcon icon;
    for (int px : { 20, 24 }) {
        for (qreal dpr : { 1.0, 2.0 }) {
            icon.addPixmap(draw(path, muted, px, dpr), QIcon::Normal, QIcon::Off);
            icon.addPixmap(draw(path, accent, px, dpr), QIcon::Normal, QIcon::On);
            icon.addPixmap(draw(path, ink, px, dpr), QIcon::Active, QIcon::Off);
            icon.addPixmap(draw(path, accent, px, dpr), QIcon::Active, QIcon::On);
            icon.addPixmap(draw(path, off, px, dpr), QIcon::Disabled, QIcon::Off);
        }
    }
    return icon;
}

}  // namespace bld::ui::theme
