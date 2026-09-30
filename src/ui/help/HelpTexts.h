#pragma once

// The help behind every "?" button (HelpButton), in plain words for club
// members: a title, `shortText` (one sentence, shown on hover) and `more`
// (two or three, shown on click). The web app's apps/web/src/help/
// helpTexts.ts has the same keys and the same words; fixtures/help-keys.txt
// is its key list, and HelpTextsTest keeps the two in step. A few keys
// are for controls only the desktop has (desktopOnlyHelpKeys()).
//
// In the words people see, layers are "sheets" and the venue is the
// "room". Keys keep their own names and never change once shipped.

#include <QString>
#include <QStringList>

#include <optional>

namespace bld::ui::help {

struct HelpEntry {
    QString title;
    QString shortText;
    QString more;
    // The web's in-app help page for this, e.g. "/help#sheets" (a path,
    // never a full address), or empty. helpPageFor() finds the desktop's.
    QString learnMoreUrl;
};

// The entry for `key`, translated; nothing for an unknown key.
std::optional<HelpEntry> helpEntry(const QString& key);

// Every key, in catalogue order: the web's first, then the desktop's own.
QStringList helpKeys();
// The keys the web app has too (the same list as fixtures/help-keys.txt).
QStringList sharedHelpKeys();
// Keys for controls only the desktop app has.
QStringList desktopOnlyHelpKeys();

}  // namespace bld::ui::help
