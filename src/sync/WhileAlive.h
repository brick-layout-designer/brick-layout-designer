#pragma once

// A server's answer for a window that may be gone by the time it comes.
//
// LibraryApi (and ServerLibrary, which holds one) outlives the windows that
// ask it things: the Members window, a closed Share dialog, a Server window
// torn down while its lists still load. Its replies belong to the api, so a
// handler that captured the window's `this` would run on a deleted window.
// Wrapping the handler with whileAlive(window, ...) drops the answer once
// the window is gone (the request itself still finishes on the server).

#include <QObject>
#include <QPointer>

#include <utility>

namespace bld::sync {

template <typename F>
auto whileAlive(QObject* owner, F&& handler) {
    return [owner = QPointer<QObject>(owner), handler = std::forward<F>(handler)](auto&&... args) mutable {
        if (owner) handler(std::forward<decltype(args)>(args)...);
    };
}

}  // namespace bld::sync
