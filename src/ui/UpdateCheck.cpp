#include "UpdateCheck.h"

#include "core/Version.h"

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
#include <QWidget>

namespace bld::ui {

namespace {

const QString kLatestReleaseApi = QStringLiteral(
    "https://api.github.com/repos/brick-layout-designer/brick-layout-designer/releases/latest");
const QString kStartupKey = QStringLiteral("updates/checkAtStartup");
const QString kLastCheckKey = QStringLiteral("updates/lastCheck");
const QString kSkippedKey = QStringLiteral("updates/skippedVersion");

}  // namespace

bool isNewerVersion(const QString& candidate, const QString& current) {
    return core::compareVersions(candidate, current) == 1;
}

bool isImportantRelease(const QString& notes) {
    static const QRegularExpression marker(QStringLiteral("\\[(important|security)\\]"),
                                           QRegularExpression::CaseInsensitiveOption);
    return marker.match(notes).hasMatch();
}

ReleaseInfo readRelease(const QJsonObject& release) {
    ReleaseInfo r;
    r.version = release.value(QStringLiteral("tag_name")).toString().trimmed();
    r.version.remove(QRegularExpression(QStringLiteral("^[vV]")));
    r.url = release.value(QStringLiteral("html_url")).toString(core::desktopDownloadUrl());
    const QString body = release.value(QStringLiteral("body")).toString().trimmed();
    r.important = isImportantRelease(body);
    r.notes = body.size() > 1500 ? body.left(1500) + QStringLiteral("…") : body;
    return r;
}

UpdateCheck::UpdateCheck(QWidget* parent)
    : QObject(parent), parent_(parent), network_(new QNetworkAccessManager(this)) {}

bool UpdateCheck::checkAtStartupEnabled() { return QSettings().value(kStartupKey, true).toBool(); }
void UpdateCheck::setCheckAtStartupEnabled(bool on) { QSettings().setValue(kStartupKey, on); }

QString UpdateCheck::skippedVersion() { return QSettings().value(kSkippedKey).toString(); }
void UpdateCheck::skipVersion(const QString& version) { QSettings().setValue(kSkippedKey, version); }

void UpdateCheck::checkNow() { check(true); }

void UpdateCheck::offer(const ReleaseInfo& release, const QString& current) {
    if (!isNewerVersion(release.version, current)) return;
    if (!release.important && release.version == skippedVersion()) return;
    emit updateAvailable(release);
}

void UpdateCheck::checkAtStartupIfDue() {
    if (!checkAtStartupEnabled()) return;
    const QDateTime last = QSettings().value(kLastCheckKey).toDateTime();
    constexpr qint64 kDaySeconds = qint64{ 24 } * 3600;
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < kDaySeconds) return;
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
        const ReleaseInfo release = readRelease(QJsonDocument::fromJson(reply->readAll()).object());
        if (!interactive) {
            offer(release, current);
            return;
        }
        if (!isNewerVersion(release.version, current)) {
            QMessageBox::information(parent_, tr("Check for Updates"),
                                     tr("You have the latest version (%1).").arg(current));
            return;
        }
        QMessageBox box(QMessageBox::Information, tr("Update available"),
                        tr("Brick Layout Designer %1 is available (you have %2).").arg(release.version, current),
                        QMessageBox::NoButton, parent_);
        if (!release.notes.isEmpty()) box.setDetailedText(release.notes);
        auto* download = box.addButton(tr("Download…"), QMessageBox::AcceptRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == download) QDesktopServices::openUrl(QUrl(release.url));
    });
}

}  // namespace bld::ui
