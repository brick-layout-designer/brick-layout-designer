// Headless live-sync client for end-to-end tests against a real server
// (the web repo's e2e/test/desktopLiveSync.spec.ts). It holds one
// SyncSession, as the desktop editor does, and is driven by a line-based
// script on stdin; every command answers with exactly one line on stdout,
// "ok[ <result>]" or "error <message>".
//
//   bld_sync_driver <server url>        e.g. http://localhost:3000
//   token in BLD_SYNC_TOKEN (a bld_pat_ API token)
//
// Commands:
//   open <layoutId>            connect to the layout's socket
//   wait-synced [ms]           until connected, in step and loaded
//   status                     Offline | Connecting | Syncing | Synced, then offline edits
//   move <brickId> <dx> <dy>   move a brick (studs), as one edit
//   rotate <brickId> <deg>     turn a brick about its centre, as one edit
//   undo | redo                this desktop's own edits only; ok true|false
//   name <display name>        who we are in presence
//   cursor <x> <y>             publish our cursor (studs)
//   disconnect | reconnect     close the connection / open it again
//   resolve mine|server|merge-default
//                              resolve offline edits: clashes go to my side,
//                              the server's, or the merge default (server)
//   dump [shared]              the editor's bricks as JSON (shared: the shared layout)
//   peers                      everyone else's presence as JSON
//   quit

#include "sync/LayoutMerge.h"
#include "sync/Presence.h"
#include "sync/SyncSession.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

#include <QDateTime>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <iostream>
#include <string>

using namespace bld;
using Status = sync::SyncClient::Status;

namespace {

// Reads stdin on its own thread so the event loop keeps the socket alive
// between commands.
class StdinReader : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;
signals:
    void line(const QString& text);
    void closed();

protected:
    void run() override {
        std::string s;
        while (std::getline(std::cin, s)) emit line(QString::fromStdString(s).trimmed());
        emit closed();
    }
};

void reply(const QString& text) {
    std::fputs(text.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Spin the event loop until `done` or `ms` pass.
bool waitFor(const std::function<bool()>& done, int ms) {
    const qint64 until = QDateTime::currentMSecsSinceEpoch() + ms;
    while (!done()) {
        if (QDateTime::currentMSecsSinceEpoch() > until) return false;
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

const char* statusName(Status s) {
    switch (s) {
    case Status::Offline: return "Offline";
    case Status::Connecting: return "Connecting";
    case Status::Syncing: return "Syncing";
    case Status::Synced: return "Synced";
    }
    return "?";
}

core::Brick* findBrick(core::Map& map, const QString& id) {
    for (auto& layer : map.layers())
        if (layer->kind() == core::LayerKind::Brick)
            for (auto& b : static_cast<core::LayerBrick&>(*layer).bricks)
                if (b.guid == id) return &b;
    return nullptr;
}

QJsonArray bricksJson(const core::Map& map) {
    QJsonArray out;
    for (const auto& layer : map.layers())
        if (layer->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
                out.append(QJsonObject{ { QStringLiteral("id"), b.guid },
                                        { QStringLiteral("layer"), layer->guid },
                                        { QStringLiteral("part"), b.partNumber },
                                        { QStringLiteral("x"), b.displayArea.x() },
                                        { QStringLiteral("y"), b.displayArea.y() },
                                        { QStringLiteral("width"), b.displayArea.width() },
                                        { QStringLiteral("height"), b.displayArea.height() },
                                        { QStringLiteral("orientation"), b.orientation } });
    return out;
}

class Driver {
public:
    explicit Driver(QUrl server) : server_(std::move(server)) {}

    // One command; false to quit.
    bool run(const QString& line) {
        const QStringList a = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (a.isEmpty()) return true;
        const QString& cmd = a[0];
        if (cmd == QLatin1String("quit")) {
            session_.close();
            reply(QStringLiteral("ok"));
            return false;
        }
        if (cmd == QLatin1String("open") && a.size() == 2) {
            layoutId_ = a[1];
            socket_ = server_;
            socket_.setScheme(server_.scheme() == QLatin1String("https") ? QStringLiteral("wss") : QStringLiteral("ws"));
            socket_.setPath(QStringLiteral("/ws/layout/") + layoutId_);
            session_.open(socket_, token_, false);
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("wait-synced")) {
            const int ms = a.size() > 1 ? a[1].toInt() : 15000;
            reply(waitFor([&] { return session_.status() == Status::Synced && session_.loaded(); }, ms)
                      ? QStringLiteral("ok")
                      : QStringLiteral("error not synced (%1)").arg(QLatin1String(statusName(session_.status()))));
        } else if (cmd == QLatin1String("status")) {
            reply(QStringLiteral("ok %1 %2").arg(QLatin1String(statusName(session_.status()))).arg(session_.unsyncedEdits()));
        } else if (cmd == QLatin1String("move") && a.size() == 4) {
            edit(a[1], [&](core::Brick& b) { b.displayArea.translate(a[2].toDouble(), a[3].toDouble()); });
        } else if (cmd == QLatin1String("rotate") && a.size() == 3) {
            edit(a[1], [&](core::Brick& b) {
                const float deg = a[2].toFloat();
                b.orientation = std::fmod(b.orientation + deg + 360.0f, 360.0f);
                // A quarter turn swaps the box about its centre, as the editor
                // does for a plain rectangular part (no parts library here).
                if (std::fmod(std::abs(deg), 180.0f) == 90.0f) {
                    const QPointF c = b.displayArea.center();
                    b.displayArea.setSize(b.displayArea.size().transposed());
                    b.displayArea.moveCenter(c);
                }
            });
        } else if (cmd == QLatin1String("undo")) {
            reply(session_.undo() ? QStringLiteral("ok true") : QStringLiteral("ok false"));
        } else if (cmd == QLatin1String("redo")) {
            reply(session_.redo() ? QStringLiteral("ok true") : QStringLiteral("ok false"));
        } else if (cmd == QLatin1String("name") && a.size() > 1) {
            name_ = a.mid(1).join(QLatin1Char(' '));
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("cursor") && a.size() == 3) {
            const sync::presence::User user{ QStringLiteral("desktop-driver"), name_,
                                             sync::presence::colorFor(QStringLiteral("desktop-driver"), layoutId_) };
            session_.setPresence(sync::presence::state(user, QPointF(a[1].toDouble(), a[2].toDouble()), {},
                                                       QDateTime::currentMSecsSinceEpoch()));
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("disconnect")) {
            session_.close();
            reply(waitFor([&] { return session_.status() == Status::Offline; }, 5000) ? QStringLiteral("ok")
                                                                                      : QStringLiteral("error still connected"));
        } else if (cmd == QLatin1String("reconnect")) {
            session_.open(socket_, token_, false);
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("resolve") && a.size() == 2) {
            resolve(a[1]);
        } else if (cmd == QLatin1String("dump")) {
            QString error;
            const bool shared = a.size() > 1 && a[1] == QLatin1String("shared");
            const auto map = shared ? session_.currentMap(&error) : session_.editorMap(&error);
            reply(map ? QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(bricksJson(*map)).toJson(QJsonDocument::Compact))
                      : QStringLiteral("error ") + error);
        } else if (cmd == QLatin1String("peers")) {
            QJsonArray peers;
            for (const auto& state : session_.peers()) peers.append(state);
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(peers).toJson(QJsonDocument::Compact)));
        } else {
            reply(QStringLiteral("error unknown command: ") + line);
        }
        return true;
    }

    void setToken(const QString& token) { token_ = token; }

private:
    // One editor edit: change a brick of what the editor shows, then hand
    // the whole map to the session, as MapView's undo stack does.
    void edit(const QString& id, const std::function<void(core::Brick&)>& change) {
        QString error;
        auto map = session_.editorMap(&error);
        if (!map) return reply(QStringLiteral("error ") + error);
        core::Brick* b = findBrick(*map, id);
        if (!b) return reply(QStringLiteral("error no brick ") + id);
        change(*b);
        session_.localEdit(*map);
        reply(QStringLiteral("ok"));
    }

    // What MainWindow::onReviewOfflineEdits does, with the compare
    // window's choices given up front.
    void resolve(const QString& how) {
        const auto& off = session_.offlineEdits();
        if (!off) return reply(QStringLiteral("error no offline edits"));
        if (session_.status() != Status::Synced) return reply(QStringLiteral("error not synced"));
        const auto current = session_.currentMap();
        if (!current) return reply(QStringLiteral("error unreadable layout"));
        const auto server = sync::merge::snapshotOf(*current);
        const auto changes = sync::merge::compareLayouts(off->base, off->mine, server);
        QHash<QString, sync::merge::Choice> choices;
        if (how == QLatin1String("mine") || how == QLatin1String("server")) {
            const auto side = how == QLatin1String("mine") ? sync::merge::Choice::Mine : sync::merge::Choice::Server;
            for (const auto& c : changes)
                if (c.status == sync::merge::Status::Conflict) choices.insert(c.key, side);
        } else if (how != QLatin1String("merge-default")) {
            return reply(QStringLiteral("error resolve mine|server|merge-default"));
        }
        QString error;
        const auto merged = sync::merge::mergeLayouts(off->mine, server, changes, choices, &error);
        if (!merged) return reply(QStringLiteral("error ") + error);
        session_.resolveOffline(merged.get());
        QJsonArray out;
        for (const auto& c : changes) out.append(c.key);
        reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
    }

    QUrl server_;
    QUrl socket_;
    QString token_;
    QString layoutId_;
    QString name_ = QStringLiteral("Desktop");
    sync::SyncSession session_;
};

}  // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 2) {
        std::fputs("usage: bld_sync_driver <server url>  (token in BLD_SYNC_TOKEN)\n", stderr);
        return 2;
    }
    Driver driver{ QUrl(args[1]) };
    driver.setToken(qEnvironmentVariable("BLD_SYNC_TOKEN"));

    // Commands run one at a time from the main loop; lines that arrive
    // while one waits are queued.
    std::deque<QString> queue;
    bool eof = false;
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::line, &app, [&](const QString& l) { queue.push_back(l); });
    QObject::connect(&reader, &StdinReader::closed, &app, [&] { eof = true; });
    reader.start();

    for (;;) {
        if (!waitFor([&] { return !queue.empty() || eof; }, 1 << 30)) continue;
        if (queue.empty()) break;
        const QString line = queue.front();
        queue.pop_front();
        if (!driver.run(line)) break;
    }
    // stdin may still be open; don't wait on the reader.
    reader.terminate();
    reader.wait(1000);
    return 0;
}

#include "bld_sync_driver.moc"
