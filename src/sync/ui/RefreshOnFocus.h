#pragma once

// Refetch what a window shows from the server when you come back to it.
// Someone may have added a venue or a layout on the web (or another
// computer) while you were away; switching back to the window asks the
// server again, at most once every `minGapMs`. The first activation (the
// window opening, which loads its list anyway) doesn't count.
//
// Why not the server's live hints (GET /api/events, which the web app
// uses)? The desktop's server lists are short-lived dialogs (Download
// venues, Open layout, Servers), and the live layout itself already
// syncs over its WebSocket. Holding a second long-lived stream per server
// for dialogs that are open for seconds costs more (a reconnecting
// streaming client, one per server, kept alive behind proxies) than it
// gives; asking again when you come back to the window covers the
// "I just added it on the web" case.

#include <QElapsedTimer>
#include <QObject>

#include <functional>

class QWidget;

namespace bld::sync {

class RefreshOnFocus : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultGapMs = 10000;

    RefreshOnFocus(QWidget* watched, std::function<void()> refresh, int minGapMs = kDefaultGapMs);

    /** Tests shorten the wait between refreshes. */
    void setMinGap(int ms) { minGapMs_ = ms; }
    /** How many times it has refreshed (tests). */
    int refreshCount() const { return count_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::function<void()> refresh_;
    QElapsedTimer since_;
    int minGapMs_;
    bool away_ = false;
    int count_ = 0;
};

} // namespace bld::sync
