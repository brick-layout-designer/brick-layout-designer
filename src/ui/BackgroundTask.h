#pragma once

#include <QApplication>
#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

namespace bld::ui {

// Cooperative-cancel token shared between the caller's work lambda
// and the UI thread's progress dialog. The work checks `requested()`
// at its own natural checkpoints; the cancel button just sets the
// flag. Atomic so the cross-thread read on `requested()` is well-
// defined without the work having to acquire a mutex.
struct CancelToken {
    std::atomic<bool> flag{false};
    bool requested() const { return flag.load(std::memory_order_acquire); }
    void cancel() { flag.store(true, std::memory_order_release); }
};

// Run a CPU-bound `work` callable on a background thread and show a
// modal busy progress dialog on the UI thread while it executes.
// Returns true if the work completed normally, false if the user
// cancelled (the work may still have run to its next checkpoint
// since C++ has no safe thread-cancel; the calling code must treat
// a `false` return as "discard the partial result, don't proceed").
//
// The work callable receives a CancelToken& it can poll between its
// own checkpoints — typically once per "pass" of a multi-stage
// pipeline (e.g. between bake and rasterise). Work that doesn't
// check the token still runs to completion; only the dialog
// disappears earlier.
//
// Usage:
//
//   bool ok = ui::runBackground(parent, tr("Loading model..."),
//       [&](ui::CancelToken& cancel){
//           result1 = stage1();
//           if (cancel.requested()) return;
//           result2 = stage2();
//       });
//   if (!ok) return;
inline bool runBackground(QWidget* parent, const QString& busyText,
                           std::function<void(CancelToken&)> work) {
    QProgressDialog dlg(busyText, QObject::tr("Cancel"), 0, 0, parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);

    CancelToken token;
    bool userCancelled = false;
    auto cancelConn = QObject::connect(&dlg, &QProgressDialog::canceled, &dlg,
        [&token, &userCancelled]{
            userCancelled = true;
            token.cancel();
        });

    // Run the work on a one-shot QThread so the UI thread stays free
    // to pump paint events. Using QThread directly (rather than
    // QtConcurrent::run) keeps Qt6::Concurrent out of bld_ui's link
    // line for one helper.
    class WorkerThread : public QThread {
    public:
        std::function<void()> fn;
        void run() override { fn(); }
    };
    WorkerThread thread;
    thread.fn = [&]{ work(token); };

    QEventLoop loop;
    QObject::connect(&thread, &QThread::finished, &loop, &QEventLoop::quit);

    dlg.show();
    QApplication::processEvents();         // paint dialog before blocking
    thread.start();
    loop.exec();                           // pumps events while waiting

    // Drop the cancel handler BEFORE closing the dialog. Otherwise
    // QProgressDialog::close() emits canceled() during its standard
    // teardown and our handler sets the token — causing the helper to
    // report cancellation on every successful run.
    QObject::disconnect(cancelConn);
    dlg.close();
    thread.wait();                         // already finished (or cancelled mid-checkpoint)
    return !userCancelled;
}

// Run `work` on a worker thread while pumping the UI thread's event loop,
// without a dialog of its own — for callers that already show progress
// (batch import). `token` is shared with that caller's cancel button.
// Returns false if the token was cancelled.
inline bool runOnWorker(CancelToken& token, const std::function<void(CancelToken&)>& work) {
    class WorkerThread : public QThread {
    public:
        std::function<void()> fn;
        void run() override { fn(); }
    };
    WorkerThread thread;
    thread.fn = [&]{ work(token); };
    QEventLoop loop;
    QObject::connect(&thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread.start();
    loop.exec();
    thread.wait();
    return !token.requested();
}

// Convenience overload for callers that don't want a cancel token.
// Same as the above but the work runs ignoring cancellation. Cancel
// button still appears so the user can dismiss the dialog after the
// work finishes; the returned bool tells the caller whether to
// proceed.
inline bool runBackground(QWidget* parent, const QString& busyText,
                           std::function<void()> work) {
    return runBackground(parent, busyText,
        [w = std::move(work)](CancelToken&){ w(); });
}

}  // namespace bld::ui
