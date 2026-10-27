#include "ClubLogoViews.h"

#include "ClubLogos.h"
#include "ServerList.h"

#include <QComboBox>
#include <QLabel>
#include <QPixmap>

namespace bld::sync {

namespace {

bool sameServer(const QUrl& a, const QUrl& b) { return a.isValid() && ServerList::folderName(a) == ServerList::folderName(b); }

void applyLogos(QComboBox* box, const QUrl& server) {
    const ClubLogos& logos = ClubLogos::shared();
    for (int i = 0; i < box->count(); ++i) {
        const QString slug = box->itemData(i).toString();
        box->setItemIcon(i, slug.isEmpty() ? QIcon() : logos.icon(server, slug));
    }
}

void showLogo(QLabel* label, const QImage& img, int size) {
    label->setVisible(!img.isNull());
    if (img.isNull()) {
        label->clear();
        return;
    }
    const qreal dpr = label->devicePixelRatioF();
    QPixmap px = QPixmap::fromImage(img.scaled(QSize(size, size) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    px.setDevicePixelRatio(dpr);
    label->setPixmap(px);
}

}  // namespace

void showClubLogos(QComboBox* box, std::function<QUrl()> server) {
    if (!box || !server) return;
    applyLogos(box, server());
    if (box->property("clubLogosFollow").toBool()) return;
    box->setProperty("clubLogosFollow", true);
    QObject::connect(&ClubLogos::shared(), &ClubLogos::changed, box, [box, server = std::move(server)](const QUrl& changed) {
        const QUrl now = server();
        if (sameServer(changed, now)) applyLogos(box, now);
    });
}

QLabel* clubLogoLabel(const QUrl& server, const QString& slug, int size, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("clubLogo"));
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setAccessibleName(QObject::tr("Club logo"));
    showLogo(label, ClubLogos::shared().logo(server, slug), size);
    QObject::connect(&ClubLogos::shared(), &ClubLogos::changed, label, [label, server, slug, size](const QUrl& changed) {
        if (sameServer(changed, server)) showLogo(label, ClubLogos::shared().logo(server, slug), size);
    });
    return label;
}

}  // namespace bld::sync
