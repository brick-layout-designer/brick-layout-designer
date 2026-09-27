#include "UpdateCheck.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>
#include <QWidget>

namespace bld::ui {

namespace {

const QString kLatestReleaseApi = QStringLiteral(
    "https://api.github.com/repos/brick-layout-designer/brick-layout-designer/releases/latest");
const QString kReleasesPage = QStringLiteral(
    "https://github.com/brick-layout-designer/brick-layout-designer/releases/latest");
const QString kStartupKey = QStringLiteral("updates/checkAtStartup");
const QString kLastCheckKey = QStringLiteral("updates/lastCheck");

struct Version {
    QVersionNumber number;
    QString preRelease;  // empty for a release
    bool valid = false;
};

Version parse(QString v) {
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V'))) v.remove(0, 1);
    static const QRegularExpression re(QStringLiteral("^(\\d+(?:\\.\\d+)*)(?:-([0-9A-Za-z.-]+))?(?:\\+.*)?$"));
    const auto m = re.match(v);
    if (!m.hasMatch()) return {};
    return { QVersionNumber::fromString(m.captured(1)), m.captured(2), true };
}

}  // namespace

bool isNewerVersion(const QString& candidate, const QString& current) {
    const Version a = parse(candidate), b = parse(current);
    if (!a.valid || !b.valid) return false;
    const int c = QVersionNumber::compare(a.number.normalized(), b.number.normalized());
    if (c != 0) return c > 0;
    // Same numbers: a release beats its pre-releases.
    if (a.preRelease.isEmpty() != b.preRelease.isEmpty()) return a.preRelease.isEmpty();
    return false;
}

UpdateCheck::UpdateCheck(QWidget* parent)
    : QObject(parent), parent_(parent), network_(new QNetworkAccessManager(this)) {}

bool UpdateCheck::checkAtStartupEnabled() { return QSettings().value(kStartupKey, true).toBool(); }
void UpdateCheck::setCheckAtStartupEnabled(bool on) { QSettings().setValue(kStartupKey, on); }

void UpdateCheck::checkNow() { check(true); }

void UpdateCheck::checkAtStartupIfDue() {
    if (!checkAtStartupEnabled()) return;
    const QDateTime last = QSettings().value(kLastCheckKey).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 3600) return;
    check(false);
}

void UpdateCheck::check(bool interactive) {
    QNetworkRequest request{ QUrl(kLatestReleaseApi) };
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BrickLayoutDesigner/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(15000);
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        const QString current = QCoreApplication::applicationVersion();
        if (reply->error() != QNetworkReply::NoError) {
            if (interactive)
                QMessageBox::warning(parent_, tr("Check for Updates"),
                    tr("Could not reach GitHub to check for updates:\n%1").arg(reply->errorString()));
            return;
        }
        QSettings().setValue(kLastCheckKey, QDateTime::currentDateTimeUtc());
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value(QStringLiteral("tag_name")).toString();
        if (!isNewerVersion(tag, current)) {
            if (interactive)
                QMessageBox::information(parent_, tr("Check for Updates"),
                    tr("You have the latest version (%1).").arg(current));
            return;
        }
        const QString url = release.value(QStringLiteral("html_url")).toString(kReleasesPage);
        QString notes = release.value(QStringLiteral("body")).toString().trimmed();
        if (notes.size() > 1500) notes = notes.left(1500) + QStringLiteral("…");
        QMessageBox box(QMessageBox::Information, tr("Update available"),
                        tr("Brick Layout Designer %1 is available (you have %2).")
                            .arg(QString(tag).remove(QRegularExpression(QStringLiteral("^[vV]"))), current),
                        QMessageBox::NoButton, parent_);
        if (!notes.isEmpty()) box.setDetailedText(notes);
        auto* download = box.addButton(tr("Download…"), QMessageBox::AcceptRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == download) QDesktopServices::openUrl(QUrl(url));
    });
}

}  // namespace bld::ui
