#pragma once

// The help pages the app ships under help/<language>/ (installed next to
// the app, or the source tree's in a development build): Help › Contents,
// Help › Getting started and the "Learn more" links in help popovers.
// Only local pages: never a server address.

#include <QString>
#include <QUrl>

#include <functional>

namespace bld::ui::help {

// The shipped page `file` (e.g. "index.html"), in the UI language if there
// is one, else English; empty when it isn't installed.
QString helpPagePath(const QString& file);

// The short page Help › Getting started opens, with the sections the
// web's /help page has (getting-started, sheets, room, sharing, files,
// shortcuts).
inline constexpr const char* kGettingStartedPage = "Getting_Started.htm";

// Where "Learn more" goes for an entry's web path ("/help#sheets"): that
// section of the Getting started page, or an empty URL when the page
// isn't installed or `webPath` isn't one of its sections.
QUrl helpPageFor(const QString& webPath);

// Opens a help page in the browser. Tests swap the opener; the previous
// one is returned.
using UrlOpener = std::function<void(const QUrl&)>;
void openHelpPage(const QUrl& url);
UrlOpener setHelpPageOpener(UrlOpener opener);

}  // namespace bld::ui::help
