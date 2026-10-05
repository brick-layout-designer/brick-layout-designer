#include "SettingsDialog.h"

#include "help/HelpButton.h"
#include "theme/AppPrefs.h"
#include "theme/Tokens.h"
#include "tours/Tours.h"

#include "ServerList.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace bld::ui {

using namespace theme;

namespace {

Mode modeOf(const QPalette& pal) { return pal.color(QPalette::Window).lightness() < 128 ? Mode::Dark : Mode::Light; }

QFont headingFont(double scale) {
    QFont f = QApplication::font();
    f.setFamily(QLatin1String(kHeadingFamily));
    f.setWeight(QFont::Bold);
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * scale);
    return f;
}

// The three theme cards' little pictures.
QPixmap themePreview(ThemeChoice t) {
    const Neutrals& l = neutrals(Mode::Light);
    const Neutrals& d = neutrals(Mode::Dark);
    const QSize size(150, 76);
    QPixmap pm(size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(0.5, 0.5, size.width() - 1, size.height() - 1);
    QPainterPath clip;
    clip.addRoundedRect(r, Radius::control, Radius::control);
    p.setClipPath(clip);
    const auto page = [&p](const QRectF& area, const Neutrals& n) {
        p.fillRect(area, n.bg);
        p.setPen(Qt::NoPen);
        p.setBrush(n.panel);
        p.drawRoundedRect(QRectF(area.left() + 7, area.top() + 7, area.width() * 0.22, area.height() - 14), 5, 5);
        p.setBrush(n.gridLine);
        p.drawRoundedRect(QRectF(area.left() + 13 + area.width() * 0.22, area.top() + 7,
                                 area.width() * 0.78 - 20, area.height() - 14), 5, 5);
    };
    if (t == ThemeChoice::System) {
        p.fillRect(QRectF(r.left(), r.top(), r.width() / 2, r.height()), l.bg);
        p.fillRect(QRectF(r.center().x(), r.top(), r.width() / 2, r.height()), d.bg);
    } else {
        page(r, t == ThemeChoice::Dark ? d : l);
    }
    p.setClipping(false);
    p.setPen(QPen(l.line, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, Radius::control, Radius::control);
    return pm;
}

// A colour swatch, ringed when chosen.
QIcon swatch(const QColor& colour, const QColor& ringGap, bool chosen) {
    const int s = 52;
    QPixmap pm(QSize(s, s) * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    if (chosen) {
        p.setBrush(colour);
        p.drawEllipse(QRectF(0.5, 0.5, s - 1, s - 1));
        p.setBrush(ringGap);
        p.drawEllipse(QRectF(3, 3, s - 6, s - 6));
    }
    p.setBrush(colour);
    p.drawEllipse(QRectF(5.5, 5.5, s - 11, s - 11));
    return QIcon(pm);
}

}  // namespace

SettingsDialog::SettingsDialog(PrefsStore& store, const QString& syncedHost,
                               const std::function<void()>& openMoreOptions, QWidget* parent,
                               const std::function<void(const QString&)>& startTour)
    : QDialog(parent), store_(store) {
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(tr("Settings"));
    resize(760, 780);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("SettingsPage"));
    page->setAttribute(Qt::WA_StyledBackground);
    scroll->setWidget(page);
    outer->addWidget(scroll, 1);

    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(4 * kSpacing, 3 * kSpacing, 4 * kSpacing, 3 * kSpacing);
    col->setSpacing(2 * kSpacing + 4);

    auto* title = new QLabel(tr("Settings"), page);
    title->setObjectName(QStringLiteral("SettingsTitle"));
    title->setFont(headingFont(2.0));
    col->addWidget(title);

    const auto card = [page, col](const QString& heading, const QString& helpKey = {}) {
        auto* f = new QFrame(page);
        f->setObjectName(QStringLiteral("SettingsCard"));
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(3 * kSpacing, 3 * kSpacing - 4, 3 * kSpacing, 3 * kSpacing - 4);
        v->setSpacing(2 * kSpacing);
        if (!heading.isEmpty()) {
            auto* h = new QLabel(heading, f);
            h->setObjectName(QStringLiteral("CardHeading"));
            h->setFont(headingFont(1.45));
            v->addWidget(helpKey.isEmpty() ? static_cast<QWidget*>(h) : help::withHelp(h, helpKey, f));
        }
        col->addWidget(f);
        return v;
    };

    // Light or dark.
    {
        auto* v = card(tr("Light or dark"));
        auto* row = new QHBoxLayout;
        row->setSpacing(14);
        auto* group = new QButtonGroup(this);
        const std::pair<ThemeChoice, QString> choices[] = {
            { ThemeChoice::Light, tr("Light") },
            { ThemeChoice::Dark, tr("Dark") },
            { ThemeChoice::System, tr("Match my computer") },
        };
        for (const auto& [choice, label] : choices) {
            auto* b = new QToolButton(this);
            b->setObjectName(QStringLiteral("theme_") + themeChoiceId(choice));
            b->setProperty("themeCard", true);
            b->setCheckable(true);
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIcon(QIcon(themePreview(choice)));
            b->setIconSize(QSize(150, 76));
            b->setText(label);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setCursor(Qt::PointingHandCursor);
            group->addButton(b);
            row->addWidget(b);
            connect(b, &QToolButton::toggled, this, [this, choice](bool on) {
                if (!on || loading_) return;
                AppPrefs p = store_.prefs();
                p.theme = choice;
                store_.update(p);
            });
        }
        v->addLayout(row);
    }

    // Colour and bigger text.
    {
        auto* v = card(tr("Colour"), QStringLiteral("settings.colour"));
        auto* row = new QHBoxLayout;
        row->setSpacing(2 * kSpacing);
        auto* group = new QButtonGroup(this);
        group->setObjectName(QStringLiteral("accentGroup"));
        for (const Accent& a : accents()) {
            auto* b = new QToolButton(this);
            b->setObjectName(QStringLiteral("accent_") + a.id);
            b->setProperty("swatch", true);
            b->setCheckable(true);
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIconSize(QSize(52, 52));
            b->setText(QCoreApplication::translate("bld::ui::SettingsDialog", a.label.toUtf8().constData()));
            b->setAccessibleName(b->text());
            b->setCursor(Qt::PointingHandCursor);
            group->addButton(b);
            row->addWidget(b);
            connect(b, &QToolButton::toggled, this, [this, id = a.id](bool on) {
                if (!on || loading_) return;
                AppPrefs p = store_.prefs();
                p.accent = id;
                store_.update(p);
            });
        }
        row->addStretch(1);
        v->addLayout(row);
        auto* large = new QCheckBox(tr("Bigger text and buttons"), this);
        large->setObjectName(QStringLiteral("largeText"));
        v->addWidget(help::withHelp(large, QStringLiteral("settings.largeText"), this));
        connect(large, &QCheckBox::toggled, this, [this](bool on) {
            if (loading_) return;
            AppPrefs p = store_.prefs();
            p.largeText = on;
            store_.update(p);
        });
    }

    // Help.
    {
        auto* v = card(tr("Help"));
        auto* helpIcons = new QCheckBox(tr("Show the small \"?\" buttons that explain things"), this);
        helpIcons->setObjectName(QStringLiteral("helpIcons"));
        v->addWidget(help::withHelp(helpIcons, QStringLiteral("settings.helpIcons"), this));
        connect(helpIcons, &QCheckBox::toggled, this, [this](bool on) {
            if (loading_) return;
            AppPrefs p = store_.prefs();
            p.helpIcons = on;
            store_.update(p);
        });

        // Tours: how many are done, Show tours again, and one button per tour.
        auto* toursRow = new QHBoxLayout;
        auto* toursText = new QVBoxLayout;
        auto* toursTitle = new QLabel(tr("Tours"), this);
        toursTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
        toursText->addWidget(help::withHelp(toursTitle, QStringLiteral("settings.tours"), this));
        toursNote_ = new QLabel(this);
        toursNote_->setObjectName(QStringLiteral("Muted"));
        toursText->addWidget(toursNote_);
        toursRow->addLayout(toursText, 1);
        showToursAgain_ = new QPushButton(tr("Show tours again"), this);
        showToursAgain_->setObjectName(QStringLiteral("showToursAgain"));
        showToursAgain_->setAutoDefault(false);
        connect(showToursAgain_, &QPushButton::clicked, this, [this] { tours::forgetSeen(store_); });
        toursRow->addWidget(showToursAgain_);
        v->addLayout(toursRow);
        if (startTour) {
            auto* take = new QHBoxLayout;
            auto* takeLabel = new QLabel(tr("Take a tour:"), this);
            takeLabel->setObjectName(QStringLiteral("Muted"));
            take->addWidget(takeLabel);
            for (const tours::Tour* t : tours::desktopTours()) {
                auto* b = new QPushButton(t->title, this);
                b->setObjectName(QStringLiteral("tour.") + t->id);
                b->setAutoDefault(false);
                connect(b, &QPushButton::clicked, this, [this, startTour, id = t->id] {
                    accept();
                    startTour(id);
                });
                take->addWidget(b);
            }
            take->addStretch(1);
            v->addLayout(take);
        }
    }

    // Servers: where your club's layouts, modules and these settings live.
    {
        auto* v = card(tr("Servers"), QStringLiteral("servers.pick"));
        auto* row = new QHBoxLayout;
        serversNote_ = new QLabel(this);
        serversNote_->setObjectName(QStringLiteral("serversNote"));
        serversNote_->setWordWrap(true);
        row->addWidget(serversNote_, 1);
        manageServers_ = new QPushButton(tr("Manage servers…"), this);
        manageServers_->setObjectName(QStringLiteral("manageServers"));
        manageServers_->setToolTip(tr("Add a server, sign in or out, and pick your Main one"));
        manageServers_->setAutoDefault(false);
        manageServers_->hide();
        connect(manageServers_, &QPushButton::clicked, this, [this] {
            // Not inside the button's own click.
            QTimer::singleShot(0, this, [this] {
                if (manage_) manage_();
                loadServers();
            });
        });
        row->addWidget(manageServers_);
        v->addLayout(row);
        loadServers();
    }

    syncNote_ = new QLabel(page);
    syncNote_->setWordWrap(true);
    syncNote_->setTextFormat(Qt::RichText);
    if (syncedHost.isEmpty()) {
        syncNote_->setObjectName(QStringLiteral("SyncNoteOff"));
        syncNote_->setText(tr("These settings are kept on this computer. While you're connected to a server, "
                              "they sync with your account there, both ways."));
    } else {
        syncNote_->setObjectName(QStringLiteral("SyncNote"));
        syncNote_->setText(tr("<b>Synced with %1</b><br>These settings are for this app. While you're connected "
                              "to a server, they sync with your account there, both ways.")
                               .arg(syncedHost.toHtmlEscaped()));
    }
    col->addWidget(help::withHelp(syncNote_, QStringLiteral("settings.sync"), page));
    col->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(4 * kSpacing, 2 * kSpacing, 4 * kSpacing, 2 * kSpacing);
    if (openMoreOptions) {
        auto* more = new QPushButton(tr("More options..."), this);
        more->setObjectName(QStringLiteral("moreOptions"));
        more->setToolTip(tr("Language, folders, parts folders, import and editing defaults"));
        more->setAutoDefault(false);
        buttons->addWidget(more);
        connect(more, &QPushButton::clicked, this, [openMoreOptions] { openMoreOptions(); });
    }
    buttons->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    buttons->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    outer->addLayout(buttons);

    connect(&store_, &PrefsStore::changed, this, [this] { load(); });
    load();
}

void SettingsDialog::setManageServers(const std::function<void()>& manage) {
    manage_ = manage;
    manageServers_->setVisible(static_cast<bool>(manage_));
}

void SettingsDialog::loadServers() {
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* main = list.mainServer();
    if (!main) main = list.lastUsed();
    if (list.servers().isEmpty() || !main) {
        serversNote_->setText(tr("No servers yet. Add your club's server to share layouts and modules with it, "
                                 "and to keep these settings with your account."));
        manageServers_->setText(tr("Add a server…"));
        return;
    }
    const auto others = static_cast<int>(list.servers().size()) - 1;
    QString text = main->label() == main->address() ? tr("Main: %1").arg(main->label())
                                                    : tr("Main: %1 (%2)").arg(main->label(), main->address());
    if (others > 0) text += QLatin1Char(' ') + tr("and %n more", nullptr, others);
    serversNote_->setText(text);
    manageServers_->setText(tr("Manage servers…"));
}

void SettingsDialog::load() {
    loading_ = true;
    const AppPrefs& p = store_.prefs();
    const Mode mode = modeOf(palette());
    const Neutrals& n = neutrals(mode);
    if (auto* b = findChild<QToolButton*>(QStringLiteral("theme_") + themeChoiceId(p.theme))) b->setChecked(true);
    for (const Accent& a : accents()) {
        auto* b = findChild<QToolButton*>(QStringLiteral("accent_") + a.id);
        if (!b) continue;
        b->setChecked(a.id == accent(p.accent).id);
        b->setIcon(swatch(a.main, n.panel, b->isChecked()));
    }
    findChild<QCheckBox*>(QStringLiteral("largeText"))->setChecked(p.largeText);
    findChild<QCheckBox*>(QStringLiteral("helpIcons"))->setChecked(p.helpIcons);
    const auto done = p.toursSeen.size();
    toursNote_->setText(done == 0 ? tr("You haven't finished any tours yet.")
                                  : done == 1 ? tr("You've seen one tour.") : tr("You've seen %1 tours.").arg(done));
    showToursAgain_->setEnabled(done > 0);

    const Accent& a = accent(p.accent);
    QString css = QStringLiteral(
        "QWidget#SettingsPage { background: %1; }"
        "QFrame#SettingsCard { background: %2; border: 1px solid %3; border-radius: %8px; }"
        "QLabel#Muted { color: %4; }"
        "QToolButton[themeCard=\"true\"] { background: %2; border: 1px solid %5; border-radius: 14px; padding: 10px;"
        " color: %6; font-weight: 700; }"
        "QToolButton[themeCard=\"true\"]:checked { border: 2px solid %7; }"
        "QToolButton[swatch=\"true\"] { border: none; background: transparent; color: %4; font-weight: 600; }"
        "QToolButton[swatch=\"true\"]:checked { color: %6; font-weight: 700; }");
    css = css.arg(n.bg.name(), n.panel.name(), n.line.name(), n.muted.name(), n.border.name(),
                  n.ink.name(), a.main.name())
              .arg(Radius::section);
    css += QStringLiteral("QLabel#SyncNote { background: %1; color: %2; border-radius: %3px; padding: 12px; }"
                          "QLabel#SyncNoteOff { color: %4; padding: 4px; }")
               .arg(n.okSoft.name(), n.ok.name())
               .arg(Radius::card)
               .arg(n.muted.name());
    setStyleSheet(css);
    loading_ = false;
}

}  // namespace bld::ui
