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
#include <QString>

#include <memory>
#include <utility>

namespace bld::sync {

namespace detail {

// A child of the owner, so it (and the flag it holds) goes with the owner.
class AliveToken : public QObject {
public:
    explicit AliveToken(QObject* owner) : QObject(owner) { setObjectName(name()); }
    static QString name() { return QStringLiteral("bld_whileAliveToken"); }
    std::shared_ptr<bool> flag = std::make_shared<bool>(true);
};

inline std::weak_ptr<bool> aliveFlag(QObject* owner) {
    auto* token = dynamic_cast<AliveToken*>(owner->findChild<QObject*>(AliveToken::name(), Qt::FindDirectChildrenOnly));
    if (!token) token = new AliveToken(owner);
    return token->flag;
}

}  // namespace detail

template <typename F>
auto whileAlive(QObject* owner, F&& handler) {
    return [alive = detail::aliveFlag(owner), handler = std::forward<F>(handler)](auto&&... args) mutable {
        if (!alive.expired()) handler(std::forward<decltype(args)>(args)...);
    };
}

}  // namespace bld::sync
