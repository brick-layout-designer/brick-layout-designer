#include "DownloadCenterDialog.h"
#include "../import/ArchivePath.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "../import/zip/SafeZip.h"

namespace bld::ui {

namespace {

// BlueBrick's hard-coded sources. Same URLs vanilla uses; the host
// serves an Apache-style HTML directory listing of .zip files.
constexpr const char* kOfficialUrl =
    "https://bluebrick.lswproject.com/download/package/";
constexpr const char* kNonLegoUrl =
    "https://bluebrick.lswproject.com/download/packageOther/";

constexpr int kMaxFetchBytes  = 4 * 1024 * 1024;    // search-page HTML cap
constexpr int kMaxPackageBytes = 100 * 1024 * 1024; // per-zip cap
constexpr int kFetchTimeoutMs = 20000;

}  // namespace

DownloadCenterDialog::DownloadCenterDialog(QString libraryRoot, QWidget* parent)
    : QDialog(parent), libraryRoot_(std::move(libraryRoot)) {
    setWindowTitle(tr("Download Center"));
    resize(640, 480);

    auto* root = new QVBoxLayout(this);

    // ---- Source selection group (mirrors LibraryPackageSourceForm) ----
    auto* srcGroup = new QGroupBox(tr("Search for parts packages from:"), this);
    auto* srcLayout = new QVBoxLayout(srcGroup);
    cbOfficial_ = new QCheckBox(tr("Official LEGO part library"), srcGroup);
    cbOfficial_->setChecked(true);
    cbOfficial_->setToolTip(QString::fromLatin1(kOfficialUrl));
    cbNonLego_ = new QCheckBox(tr("Non-LEGO part library (BrickTracks, 4DBrix, …)"), srcGroup);
    cbNonLego_->setToolTip(QString::fromLatin1(kNonLegoUrl));
    cbCustom_ = new QCheckBox(tr("Unofficial location (custom URL)"), srcGroup);
    customUrl_ = new QLineEdit(srcGroup);
    customUrl_->setPlaceholderText(tr("https://example.com/parts/"));
    customUrl_->setEnabled(false);
    {
        QSettings s;
        const QString lastCustom = s.value(QStringLiteral("download/customUrl")).toString();
        if (!lastCustom.isEmpty()) customUrl_->setText(lastCustom);
    }
    srcLayout->addWidget(cbOfficial_);
    srcLayout->addWidget(cbNonLego_);
    {
        auto* row = new QHBoxLayout();
        row->addWidget(cbCustom_);
        row->addWidget(customUrl_, 1);
        srcLayout->addLayout(row);
    }
    root->addWidget(srcGroup);

    // ---- Search button + status ----
    {
        auto* row = new QHBoxLayout();
        searchBtn_ = new QPushButton(tr("Search"), this);
        statusLabel_ = new QLabel(this);
        statusLabel_->setText(tr("No search yet."));
        row->addWidget(searchBtn_);
        row->addWidget(statusLabel_, 1);
        root->addLayout(row);
    }

    // ---- Candidate list ----
    auto* listLabel = new QLabel(tr("Available packages (already-installed versions filtered out):"), this);
    root->addWidget(listLabel);
    candidates_ = new QListWidget(this);
    candidates_->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(candidates_, 1);

    // ---- Progress + footer ----
    progress_ = new QProgressBar(this);
    progress_->setVisible(false);
    root->addWidget(progress_);

    {
        auto* row = new QHBoxLayout();
        row->addStretch(1);
        downloadBtn_ = new QPushButton(tr("Download && Install"), this);
        downloadBtn_->setEnabled(false);
        closeBtn_ = new QPushButton(tr("Close"), this);
        row->addWidget(downloadBtn_);
        row->addWidget(closeBtn_);
        root->addLayout(row);
    }

    connect(cbCustom_, &QCheckBox::toggled, this, &DownloadCenterDialog::onCustomToggled);
    connect(searchBtn_, &QPushButton::clicked, this, &DownloadCenterDialog::onSearchClicked);
    connect(downloadBtn_, &QPushButton::clicked, this, &DownloadCenterDialog::onDownloadClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &DownloadCenterDialog::reject);
}

void DownloadCenterDialog::onCustomToggled(bool on) {
    customUrl_->setEnabled(on);
}

QString DownloadCenterDialog::fetchUrl(const QUrl& url, QString* error) {
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    // Match BlueBrick's user-agent so servers that filter on UA still
    // serve us a directory listing.
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral(
        "Mozilla/5.0 (compatible; BLD/1.0; +https://github.com/brick-layout-designer/brick-layout-designer)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kFetchTimeoutMs);
    QNetworkReply* rep = nam.get(req);
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (rep->error() != QNetworkReply::NoError) {
        if (error) *error = rep->errorString();
        rep->deleteLater();
        return {};
    }
    const QByteArray bytes = rep->read(kMaxFetchBytes + 1);
    rep->deleteLater();
    if (bytes.size() > kMaxFetchBytes) {
        if (error) *error = tr("Search response too large.");
        return {};
    }
    return QString::fromUtf8(bytes);
}

QHash<QString, QString> DownloadCenterDialog::readInstalledVersions() const {
    QHash<QString, QString> out;
    QDir lib(libraryRoot_);
    if (!lib.exists()) return out;
    const QFileInfoList dirs = lib.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& d : dirs) {
        // BlueBrick stores version metadata at <pkg>/config/About.txt as
        // simple "key=value" lines, with `version=X.Y` as the marker.
        // We follow the same convention so packages produced for vanilla
        // also work here.
        const QString aboutPath = d.absoluteFilePath()
            + QStringLiteral("/config/About.txt");
        QString version;
        QFile f(aboutPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                const QString line = ts.readLine().trimmed();
                if (line.startsWith(QStringLiteral("version="), Qt::CaseInsensitive)) {
                    version = line.mid(QStringLiteral("version=").size()).trimmed();
                    break;
                }
            }
        }
        out.insert(d.fileName(), version);
    }
    return out;
}

QVector<DownloadCenterDialog::Package>
DownloadCenterDialog::filterAlreadyInstalled(QVector<Package> input) const {
    const auto installed = readInstalledVersions();
    QVector<Package> kept;
    kept.reserve(input.size());
    for (const auto& p : input) {
        const auto it = installed.constFind(p.name);
        if (it == installed.constEnd()) {
            kept.push_back(p);
            continue;
        }
        // Drop only when the installed version is >= the candidate.
        // Empty versions sort first, matching BlueBrick's string compare.
        if (QString::compare(it.value(), p.version) >= 0) continue;
        kept.push_back(p);
    }
    return kept;
}

void DownloadCenterDialog::onSearchClicked() {
    QStringList urls;
    if (cbOfficial_->isChecked()) urls.append(QString::fromLatin1(kOfficialUrl));
    if (cbNonLego_->isChecked())  urls.append(QString::fromLatin1(kNonLegoUrl));
    if (cbCustom_->isChecked()) {
        QString u = customUrl_->text().trimmed();
        if (!u.isEmpty()) {
            // Ensure trailing slash so relative <a href="X.zip"> resolves
            // against a directory base, matching how BlueBrick treats
            // these URLs.
            if (!u.endsWith(QChar('/'))) u.append(QChar('/'));
            urls.append(u);
            QSettings().setValue(QStringLiteral("download/customUrl"), u);
        }
    }
    if (urls.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
            tr("Pick at least one source to search."));
        return;
    }

    candidates_->clear();
    downloadBtn_->setEnabled(false);
    searchBtn_->setEnabled(false);
    statusLabel_->setText(tr("Searching..."));
    QApplication::processEvents();

    // Same regex BlueBrick uses on the directory listing's HTML body.
    // The capture group is the displayed filename ("name.zip" or
    // "name.X.Y.zip"). We trust the source URL is a directory base so
    // sourceUrl = url + filename.
    static const QRegularExpression rx(
        QStringLiteral("<a href=\"[^\"]+\\.zip\">(?<name>[^<]+\\.zip)</a>"),
        QRegularExpression::CaseInsensitiveOption);

    QVector<Package> all;
    QStringList errors;
    for (int i = 0; i < urls.size(); ++i) {
        const QString& url = urls[i];
        statusLabel_->setText(tr("Searching %1/%2: %3").arg(i + 1).arg(urls.size()).arg(url));
        QApplication::processEvents();
        QString err;
        const QString html = fetchUrl(QUrl(url), &err);
        if (html.isEmpty()) {
            errors.append(tr("%1 — %2").arg(url, err.isEmpty() ? tr("empty response") : err));
            continue;
        }
        auto it = rx.globalMatch(html);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString filename = m.captured(QStringLiteral("name"));
            // Split off ".zip", then split on the first dot to extract a
            // version suffix. e.g. "Cars.1.2.zip" -> name="Cars", ver="1.2".
            QString stem = filename;
            stem.chop(4);  // remove ".zip"
            Package pkg;
            const int dot = stem.indexOf(QChar('.'));
            if (dot > 0) {
                pkg.name = stem.left(dot);
                pkg.version = stem.mid(dot + 1);
            } else {
                pkg.name = stem;
            }
            pkg.fileName  = filename;
            pkg.sourceUrl = url + filename;
            all.append(pkg);
        }
    }

    const QVector<Package> filtered = filterAlreadyInstalled(all);
    candidates_->clear();
    for (const auto& p : filtered) {
        const QString label = p.version.isEmpty()
            ? p.name
            : QStringLiteral("%1 (v%2)").arg(p.name, p.version);
        auto* item = new QListWidgetItem(label, candidates_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole + 0, p.sourceUrl);
        item->setData(Qt::UserRole + 1, p.fileName);
        item->setData(Qt::UserRole + 2, p.name);
        item->setData(Qt::UserRole + 3, p.version);
        item->setToolTip(p.sourceUrl);
    }

    QString status;
    if (filtered.isEmpty()) {
        if (all.isEmpty())
            status = tr("No packages found.");
        else
            status = tr("All %1 found packages are already installed.").arg(all.size());
    } else {
        status = tr("%1 package(s) available; %2 already installed.")
                    .arg(filtered.size()).arg(all.size() - filtered.size());
    }
    if (!errors.isEmpty()) status += QChar(' ') + tr("Errors:") + QChar(' ') + errors.join(QStringLiteral("; "));
    statusLabel_->setText(status);
    searchBtn_->setEnabled(true);
    downloadBtn_->setEnabled(!filtered.isEmpty());
}

bool DownloadCenterDialog::downloadAndInstall(const Package& pkg, QString* error) {
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(pkg.sourceUrl));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral(
        "Mozilla/5.0 (compatible; BLD/1.0; +https://github.com/brick-layout-designer/brick-layout-designer)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* rep = nam.get(req);
    QObject::connect(rep, &QNetworkReply::downloadProgress, this,
        [this](qint64 done, qint64 total){
            if (total <= 0) return;
            progress_->setMaximum(static_cast<int>(total));
            progress_->setValue(static_cast<int>(done));
        });
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (rep->error() != QNetworkReply::NoError) {
        if (error) *error = rep->errorString();
        rep->deleteLater();
        return false;
    }
    QByteArray bytes = rep->readAll();
    rep->deleteLater();
    if (bytes.isEmpty()) {
        if (error) *error = tr("Empty response.");
        return false;
    }
    if (bytes.size() > kMaxPackageBytes) {
        if (error) *error = tr("Refusing archives larger than %1 MB.")
                                .arg(kMaxPackageBytes / (1024 * 1024));
        return false;
    }

    QDir().mkpath(libraryRoot_);
    const import::SafeZip zip(bytes);
    if (!zip.isValid()) {
        if (error) *error = tr("Could not read the archive.");
        return false;
    }
    // Every entry is treated as hostile: package sources are user-
    // configurable URLs. Names can't escape the library root ("../"),
    // symlink entries aren't recreated, and sizes are bounded (SafeZip).
    const QDir root(libraryRoot_);
    for (const auto& entry : zip.entries()) {
        const QString abs = import::resolveArchiveEntryPath(root.absolutePath(), entry.name);
        if (abs.isEmpty() || entry.isSymLink) {
            if (error) *error = tr("Refusing unsafe archive entry: %1").arg(entry.name);
            return false;
        }
        if (entry.isDir) {
            if (!QDir().mkpath(abs)) {
                if (error) *error = tr("Could not create %1").arg(abs);
                return false;
            }
            continue;
        }
        const auto entryBytes = zip.read(entry);
        if (!entryBytes) {
            if (error) *error = tr("Damaged archive entry: %1").arg(entry.name);
            return false;
        }
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile out(abs);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(*entryBytes) < 0) {
            if (error) *error = tr("Could not extract files into %1").arg(libraryRoot_);
            return false;
        }
    }
    return true;
}

void DownloadCenterDialog::onDownloadClicked() {
    QVector<Package> selected;
    for (int i = 0; i < candidates_->count(); ++i) {
        QListWidgetItem* it = candidates_->item(i);
        if (it->checkState() != Qt::Checked) continue;
        Package p;
        p.sourceUrl = it->data(Qt::UserRole + 0).toString();
        p.fileName  = it->data(Qt::UserRole + 1).toString();
        p.name      = it->data(Qt::UserRole + 2).toString();
        p.version   = it->data(Qt::UserRole + 3).toString();
        selected.append(p);
    }
    if (selected.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
            tr("Tick at least one package to install."));
        return;
    }

    downloadBtn_->setEnabled(false);
    searchBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setMaximum(0);

    QStringList failures;
    int ok = 0;
    for (int i = 0; i < selected.size(); ++i) {
        const auto& p = selected[i];
        statusLabel_->setText(tr("Downloading %1 (%2/%3)…").arg(p.fileName).arg(i + 1).arg(selected.size()));
        QApplication::processEvents();
        QString err;
        if (downloadAndInstall(p, &err)) {
            ++ok;
            // Mark the matching row so the user can see what landed.
            for (int j = 0; j < candidates_->count(); ++j) {
                QListWidgetItem* row = candidates_->item(j);
                if (row->data(Qt::UserRole + 1).toString() != p.fileName) continue;
                row->setText(row->text() + QStringLiteral(" ✓"));
                row->setForeground(QBrush(QColor(40, 130, 60)));
                row->setCheckState(Qt::Unchecked);
                row->setFlags(row->flags() & ~Qt::ItemIsUserCheckable);
                break;
            }
        } else {
            failures.append(QStringLiteral("%1: %2").arg(p.fileName, err));
        }
    }
    progress_->setVisible(false);
    installedCount_ += ok;

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
            tr("Some downloads failed:\n%1").arg(failures.join(QChar('\n'))));
    }
    statusLabel_->setText(tr("Installed %1 package(s).").arg(ok));
    searchBtn_->setEnabled(true);
    downloadBtn_->setEnabled(false);
    if (ok > 0) {
        // Don't auto-close — the user might want to grab more. The
        // caller checks installedCount() on accept; clicking Close maps
        // to reject() but we expose installedCount() either way.
        accept();
    }
}

}  // namespace bld::ui
