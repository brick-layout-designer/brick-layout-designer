#include "HelpTexts.h"

#include <QCoreApplication>

namespace bld::ui::help {

namespace {

// Marked with QT_TRANSLATE_NOOP("HelpTexts", ...); helpEntry() translates them.
struct Raw {
    const char* key;
    const char* learnMoreUrl;
    const char* title;
    const char* shortText;
    const char* more;
};

// The web's catalogue (apps/web/src/help/helpTexts.ts), word for word and
// in the same order. Change both together.
constexpr Raw kShared[] = {
    // Panels
    { "panel.parts", "/help#getting-started",
      QT_TRANSLATE_NOOP("HelpTexts", "Parts"),
      QT_TRANSLATE_NOOP("HelpTexts", "All the pieces you can build with."),
      QT_TRANSLATE_NOOP("HelpTexts", "Search by name or number, or pick a group like track or plates. "
                                     "Drag a part onto the map, or click it to drop it in the middle.") },
    { "panel.sheets", "/help#sheets",
      QT_TRANSLATE_NOOP("HelpTexts", "Sheets"),
      QT_TRANSLATE_NOOP("HelpTexts", "Sheets are see-through pages stacked on the map, to keep things "
                                     "apart."),
      QT_TRANSLATE_NOOP("HelpTexts", "Put track on one sheet and buildings on another. Hide a sheet to "
                                     "see under it, or lock it so nothing on it moves by accident. New "
                                     "parts go on the sheet that is picked.") },
    { "panel.partsList", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Parts list"),
      QT_TRANSLATE_NOOP("HelpTexts", "Every part this layout uses, with how many of each."),
      QT_TRANSLATE_NOOP("HelpTexts", "Use it to check what you need to bring to a show. If the layout has "
                                     "a budget, parts over their limit are marked.") },
    { "panel.modules", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Modules"),
      QT_TRANSLATE_NOOP("HelpTexts", "Groups of pieces kept together, so you can move or reuse them as "
                                     "one."),
      QT_TRANSLATE_NOOP("HelpTexts", "A module is like a table section: pick it to select everything in "
                                     "it at once. Save a module to reuse it in other layouts.") },
    { "panel.moduleLibrary", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Module library"),
      QT_TRANSLATE_NOOP("HelpTexts", "Modules saved to your account or your club, ready to drop in."),
      QT_TRANSLATE_NOOP("HelpTexts", "Drag a module from here onto the map to add a copy of it. Your "
                                     "club’s modules are shared with everyone in the club. To change a module, "
                                     "open it from Home: it opens on its own, and Save module keeps your changes.") },
    { "panel.roomLibrary", "/help#room",
      QT_TRANSLATE_NOOP("HelpTexts", "Venue library"),
      QT_TRANSLATE_NOOP("HelpTexts", "Venues saved to your account or your club."),
      QT_TRANSLATE_NOOP("HelpTexts", "Put a venue under your layout to check that it fits, with space to "
                                     "walk around. Anyone in your club can use the club’s venues.") },
    { "panel.views", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Views"),
      QT_TRANSLATE_NOOP("HelpTexts", "Saved views remember a part of the layout, so you can show it again "
                                     "in one tap."),
      QT_TRANSLATE_NOOP("HelpTexts", "A view can fit the whole layout or keep one area, and show only some "
                                     "sheets. Share a picture of any view, or export a picture of every "
                                     "view at once after a change.") },
    // Top bar
    { "topbar.saveStatus", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Saving"),
      QT_TRANSLATE_NOOP("HelpTexts", "Shows whether your changes are saved; everything saves by itself."),
      QT_TRANSLATE_NOOP("HelpTexts", "“Saved” means the server has everything. If you go offline, keep "
                                     "working: your changes are kept here and saved when you are back.") },
    { "topbar.tasks", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Tasks"),
      QT_TRANSLATE_NOOP("HelpTexts", "Jump to what you want to do: build, draw the venue, add notes or see "
                                     "the parts list."),
      QT_TRANSLATE_NOOP("HelpTexts", "Each task opens the panels you need for it. Nothing is lost when "
                                     "you switch between them.") },
    { "people.here", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "People here now"),
      QT_TRANSLATE_NOOP("HelpTexts", "The people working on this layout right now."),
      QT_TRANSLATE_NOOP("HelpTexts", "Everyone sees each other’s changes as they happen. A faded circle "
                                     "means that person has stepped away for a bit.") },
    // Toolbar and tool rail
    { "toolbar.snap", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Snap"),
      QT_TRANSLATE_NOOP("HelpTexts", "Pieces jump to the nearest grid line, so rows stay straight."),
      QT_TRANSLATE_NOOP("HelpTexts", "The number is how far apart the grid lines are, in studs. Pick "
                                     "“off” to place something exactly where you drop it. Track ends "
                                     "still click together.") },
    { "toolbar.rotateStep", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Turn step"),
      QT_TRANSLATE_NOOP("HelpTexts", "How far a piece turns each time you press R."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick 90° for square turns, or a smaller step for curves and angles. "
                                     "Shift+R turns the other way.") },
    { "toolbar.paintColour", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Paint colour"),
      QT_TRANSLATE_NOOP("HelpTexts", "The colour the Paint tool uses."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick a colour here, then choose Paint and click on the map to "
                                     "colour an area. It paints the ground, not the pieces.") },
    { "toolbar.panels", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Panels"),
      QT_TRANSLATE_NOOP("HelpTexts", "Show or hide the side panels."),
      QT_TRANSLATE_NOOP("HelpTexts", "Tick a panel to show it. You can also drag a panel by its handle to "
                                     "move it, or float it over the map.") },
    { "tools.rail", "/help#shortcuts",
      QT_TRANSLATE_NOOP("HelpTexts", "Tools"),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick what a click on the map does."),
      QT_TRANSLATE_NOOP("HelpTexts", "Select moves pieces. Measure and Circle add a ruler you can keep on "
                                     "the map. Venue and Obstacle draw the walls of the venue and things to "
                                     "keep clear of.") },
    // Status bar
    { "status.sheet", "/help#sheets",
      QT_TRANSLATE_NOOP("HelpTexts", "Current sheet"),
      QT_TRANSLATE_NOOP("HelpTexts", "New parts go on this sheet."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick a different sheet in the Sheets panel to put new parts there "
                                     "instead. Each sheet keeps its own parts.") },
    { "status.room", "/help#room",
      QT_TRANSLATE_NOOP("HelpTexts", "Venue check"),
      QT_TRANSLATE_NOOP("HelpTexts", "Says whether the layout fits the venue, with space to walk around."),
      QT_TRANSLATE_NOOP("HelpTexts", "It checks for pieces outside the walls, on top of obstacles, or too "
                                     "close to leave a walkway. Point at it to see what needs fixing.") },
    { "status.budget", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Budget"),
      QT_TRANSLATE_NOOP("HelpTexts", "Says whether the layout stays within the parts you have."),
      QT_TRANSLATE_NOOP("HelpTexts", "A budget is how many of each part you own. “Over” means the layout "
                                     "uses more of some part than you have.") },
    // Dialogs in the editor
    { "dialog.budget", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Budget"),
      QT_TRANSLATE_NOOP("HelpTexts", "Set how many of each part you have, so the layout never uses more."),
      QT_TRANSLATE_NOOP("HelpTexts", "Type a number next to a part to set its limit. Save the budget to a "
                                     "file to use it again, or open one a friend made.") },
    { "dialog.sheetOptions", "/help#sheets",
      QT_TRANSLATE_NOOP("HelpTexts", "Sheet options"),
      QT_TRANSLATE_NOOP("HelpTexts", "The name and look of this sheet."),
      QT_TRANSLATE_NOOP("HelpTexts", "Make a sheet see-through to show what is under it. Hidden and "
                                     "locked sheets stay in the layout; they just can’t be seen or "
                                     "changed.") },
    { "dialog.label", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Labels"),
      QT_TRANSLATE_NOOP("HelpTexts", "A label is a note pinned to a piece, and it moves with it."),
      QT_TRANSLATE_NOOP("HelpTexts", "Use labels to name a station or a street. If the piece moves, its "
                                     "label moves too.") },
    { "dialog.measure", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Measure"),
      QT_TRANSLATE_NOOP("HelpTexts", "A ruler on the map that shows a real distance."),
      QT_TRANSLATE_NOOP("HelpTexts", "Rulers stay on the map until you delete them, so others can see "
                                     "them too. Change its colour, or show the length in studs, metres or "
                                     "feet.") },
    { "dialog.exportImage", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Export or print"),
      QT_TRANSLATE_NOOP("HelpTexts", "Make a picture of the layout to share or print."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick the part of the map and the size. Printing can spread a big "
                                     "layout over several pages.") },
    { "dialog.exportPartList", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Export the parts list"),
      QT_TRANSLATE_NOOP("HelpTexts", "Save the list of parts as a file, for shopping or packing."),
      QT_TRANSLATE_NOOP("HelpTexts", "HTML has pictures and opens in a browser. CSV opens in a "
                                     "spreadsheet.") },
    { "dialog.moduleLook", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Module look"),
      QT_TRANSLATE_NOOP("HelpTexts", "This module’s own outline and name colours, and whether its name "
                                     "shows."),
      QT_TRANSLATE_NOOP("HelpTexts", "With Same colour on, the outline and the name change together. Reset "
                                     "to default brings back the light blue. Long names wrap, get smaller, "
                                     "and are only cut short when nothing else fits.") },
    // Downloading
    { "download.formats", "/help#files",
      QT_TRANSLATE_NOOP("HelpTexts", "Download as"),
      QT_TRANSLATE_NOOP("HelpTexts", "Save a copy of this layout on your computer."),
      QT_TRANSLATE_NOOP("HelpTexts", "The layout file keeps everything and opens in this app or the "
                                     "desktop app. The other choices are for other programs and may leave "
                                     "some things out.") },
    { "download.layout", "/help#files",
      QT_TRANSLATE_NOOP("HelpTexts", "Layout file"),
      QT_TRANSLATE_NOOP("HelpTexts", "Everything in one file: parts, sheets, venue, labels and modules."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick this to keep a backup or to open the layout in the desktop "
                                     "app. Nothing is left out.") },
    { "download.bbm", "/help#files",
      QT_TRANSLATE_NOOP("HelpTexts", "BlueBrick map"),
      QT_TRANSLATE_NOOP("HelpTexts", "For people who still use the old BlueBrick program."),
      QT_TRANSLATE_NOOP("HelpTexts", "BlueBrick can’t hold everything this app can, so the venue, labels, "
                                     "modules and background picture are left out. Your layout here keeps "
                                     "them.") },
    // Share and people
    { "share.roles", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Viewer or editor"),
      QT_TRANSLATE_NOOP("HelpTexts", "Editors can change the layout; viewers can only look."),
      QT_TRANSLATE_NOOP("HelpTexts", "Invite people by email. They get a link to join, and can see "
                                     "changes as they happen. You can change their role or remove them "
                                     "later.") },
    { "share.people", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "People"),
      QT_TRANSLATE_NOOP("HelpTexts", "Everyone who can open this layout."),
      QT_TRANSLATE_NOOP("HelpTexts", "The owner decides who else can see or change the layout. Anyone "
                                     "here can open it from their own list of layouts.") },
    { "share.pending", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Waiting to join"),
      QT_TRANSLATE_NOOP("HelpTexts", "People you invited who haven’t joined yet."),
      QT_TRANSLATE_NOOP("HelpTexts", "Invites stop working after a while. Cancel one if you sent it to "
                                     "the wrong address.") },
    { "share.transfer", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Give it to someone else"),
      QT_TRANSLATE_NOOP("HelpTexts", "Hand this layout to another person or to your club."),
      QT_TRANSLATE_NOOP("HelpTexts", "The new owner decides who can see and change it. Give it to your "
                                     "club so it stays with the club, not with one person.") },
    { "share.publicLink", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Public link"),
      QT_TRANSLATE_NOOP("HelpTexts", "Anyone with the link can look at the layout, without signing in."),
      QT_TRANSLATE_NOOP("HelpTexts", "They can look but not change anything. Turn the link off and it "
                                     "stops working straight away.") },
    { "share.history", "",
      QT_TRANSLATE_NOOP("HelpTexts", "History"),
      QT_TRANSLATE_NOOP("HelpTexts", "What happened to this layout, and when: who opened, shared or saved "
                                     "a copy of it."),
      QT_TRANSLATE_NOOP("HelpTexts", "It helps you see who has been working on the layout. It doesn’t "
                                     "list every piece that moved.") },
    { "share.picture", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Share a picture"),
      QT_TRANSLATE_NOOP("HelpTexts", "Send a picture of the layout to anyone, even people without an "
                                     "account."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick the whole layout, what is on screen, or a saved view. On a phone "
                                     "it opens your share menu; on a computer it saves the picture or "
                                     "copies it.") },
    { "share.exportAllViews", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Export all views"),
      QT_TRANSLATE_NOOP("HelpTexts", "One picture of every saved view, all in one zip file."),
      QT_TRANSLATE_NOOP("HelpTexts", "After you change the layout, do it again to get fresh pictures with "
                                     "the same names. The size you pick is remembered.") },
    // Settings
    { "settings.sync", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Where settings are kept"),
      QT_TRANSLATE_NOOP("HelpTexts", "Signed in, your settings follow you to any computer."),
      QT_TRANSLATE_NOOP("HelpTexts", "They are kept with your account on this server. Signed out, they "
                                     "are kept in this browser only.") },
    { "settings.colour", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Colour"),
      QT_TRANSLATE_NOOP("HelpTexts", "The colour of buttons and highlights; your bricks keep their own "
                                     "colours."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick the one you like best. It only changes how the app looks for "
                                     "you.") },
    { "settings.largeText", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Bigger text and buttons"),
      QT_TRANSLATE_NOOP("HelpTexts", "Makes words and buttons larger and easier to hit."),
      QT_TRANSLATE_NOOP("HelpTexts", "Handy on a small screen or if reading is tiring. The map zoom "
                                     "doesn’t change.") },
    { "settings.helpIcons", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Help buttons"),
      QT_TRANSLATE_NOOP("HelpTexts", "The small round question marks, like this one."),
      QT_TRANSLATE_NOOP("HelpTexts", "Turn them off once you know your way around. You can turn them back "
                                     "on here or from the Help menu.") },
    { "settings.tours", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Tours"),
      QT_TRANSLATE_NOOP("HelpTexts", "Short guided walks through the app."),
      QT_TRANSLATE_NOOP("HelpTexts", "A tour points at the real buttons, one step at a time. Press “Show "
                                     "tours again” to see the ones you have already finished.") },
    { "settings.connectionSnap", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Snap strength"),
      QT_TRANSLATE_NOOP("HelpTexts", "How strongly a part you drag pulls onto a matching connection nearby."),
      QT_TRANSLATE_NOOP("HelpTexts", "Gentle pulls only when the ends are close and lets go once you move away; "
                                     "Strong reaches further; Off never pulls, and the grid still lines parts up. "
                                     "Hold Alt (Option on a Mac) while dragging to place one part without "
                                     "snapping.") },
    // The venue designer
    { "room.tools", "/help#room",
      QT_TRANSLATE_NOOP("HelpTexts", "Drawing tools"),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick what to draw: walls, doors, columns, power points and more."),
      QT_TRANSLATE_NOOP("HelpTexts", "Click on the drawing to place things. While drawing, type a length "
                                     "and press Enter to make it exact.") },
    { "room.units", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Units"),
      QT_TRANSLATE_NOOP("HelpTexts", "Show lengths in feet and inches, metres or studs."),
      QT_TRANSLATE_NOOP("HelpTexts", "Change it any time: the venue stays the same size. A stud is 8 mm, "
                                     "the width of one LEGO bump.") },
    { "room.snap", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Snap"),
      QT_TRANSLATE_NOOP("HelpTexts", "Lines jump to corners, walls and straight angles as you draw."),
      QT_TRANSLATE_NOOP("HelpTexts", "Turn it off to draw freely. Hold Shift while drawing to go at any "
                                     "angle for a moment.") },
    { "room.floorPlan", "/help#room",
      QT_TRANSLATE_NOOP("HelpTexts", "Floor plan"),
      QT_TRANSLATE_NOOP("HelpTexts", "Put a picture of the venue’s plan underneath, and trace over it."),
      QT_TRANSLATE_NOOP("HelpTexts", "A photo or a drawing both work. Scale it by clicking two points you "
                                     "know the real distance between.") },
    { "room.calibrate", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Scale the floor plan"),
      QT_TRANSLATE_NOOP("HelpTexts", "Make the picture the right size by measuring one known distance."),
      QT_TRANSLATE_NOOP("HelpTexts", "Click Calibrate, then click two points on the picture, like the "
                                     "ends of a wall. Type the real distance between them and the picture "
                                     "is scaled to fit.") },
    { "room.show", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Show"),
      QT_TRANSLATE_NOOP("HelpTexts", "Hide parts of the drawing to see the rest more clearly."),
      QT_TRANSLATE_NOOP("HelpTexts", "Hiding something only hides it here. It is still part of the venue.") },
    { "room.walkway", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Walkway"),
      QT_TRANSLATE_NOOP("HelpTexts", "The space to leave clear around the layout for people to walk."),
      QT_TRANSLATE_NOOP("HelpTexts", "The venue check warns you when the layout comes closer than this to "
                                     "a wall or an obstacle. It starts at about 90 cm.") },
    { "room.estimates", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Estimates"),
      QT_TRANSLATE_NOOP("HelpTexts", "Lengths you guessed and still need to measure."),
      QT_TRANSLATE_NOOP("HelpTexts", "Mark a length as an estimate when you don’t know it yet. Measure it "
                                     "at the hall later and type in the real number.") },
    // Parts That Differ
    { "partsDiffer.about", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Parts that differ"),
      QT_TRANSLATE_NOOP("HelpTexts", "This file has its own version of some parts the server already has."),
      QT_TRANSLATE_NOOP("HelpTexts", "Maybe someone changed a part’s picture. Compare the two and choose "
                                     "which one to use.") },
    { "partsDiffer.choice", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Which to use"),
      QT_TRANSLATE_NOOP("HelpTexts", "Keep the server’s part, use the file’s, or keep both."),
      QT_TRANSLATE_NOOP("HelpTexts", "“Use the file’s” replaces the part for everyone on this server. "
                                     "“Keep both” adds the file’s under a new number, and only this "
                                     "layout uses it.") },

    // Yours and your clubs'
    { "owners.filter", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Whose things"),
      QT_TRANSLATE_NOOP("HelpTexts", "Show everything you can use, only your own, or one club’s."),
      QT_TRANSLATE_NOOP("HelpTexts", "Your own things and your clubs’ things sit in one list, each marked "
                                     "with who owns it. Pick a club to see just its things; new things are "
                                     "saved there too unless you choose otherwise.") },

    // Joining a club
    { "club.whoCanJoin", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Who can join"),
      QT_TRANSLATE_NOOP("HelpTexts", "Choose how new people get into the club."),
      QT_TRANSLATE_NOOP("HelpTexts", "Invite only: people join with an invite from an admin. Ask to join: "
                                     "people send a request and an admin says yes or no. Open: anyone "
                                     "signed in can join straight away, as a member.") },
    { "club.listed", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Show in the club list"),
      QT_TRANSLATE_NOOP("HelpTexts", "Let people find the club under Find a club."),
      QT_TRANSLATE_NOOP("HelpTexts", "Everyone signed in sees its name, its description and how many "
                                     "members it has. Leave it off to keep the club hidden, so only people "
                                     "you invite know about it.") },
    { "club.requests", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Requests to join"),
      QT_TRANSLATE_NOOP("HelpTexts", "People who asked to join the club, waiting for an admin."),
      QT_TRANSLATE_NOOP("HelpTexts", "Approve to make them a member. Decline to say no; they aren’t told, "
                                     "and they can ask again later.") },
    { "club.roles", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Roles in the club"),
      QT_TRANSLATE_NOOP("HelpTexts", "Admins run the club, managers run its day to day, members use it."),
      QT_TRANSLATE_NOOP("HelpTexts", "Admins change the club’s settings and roles, and can hand over or "
                                     "delete the club. Managers invite people, answer requests to join, "
                                     "remove members and look after the club’s things. Members use and add "
                                     "the club’s layouts, venues, modules and parts.") },
    { "club.find", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Find a club"),
      QT_TRANSLATE_NOOP("HelpTexts", "Clubs that chose to be listed here, for anyone to find."),
      QT_TRANSLATE_NOOP("HelpTexts", "Join an open club straight away, or ask to join and wait for an "
                                     "admin to say yes. Some clubs take new members by invite only.") },
};

// Controls only the desktop app has.
constexpr Raw kDesktopOnly[] = {
    // Preferences (Edit > Preferences...)
    { "prefs.budgetUnlimited", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Parts without a budget"),
      QT_TRANSLATE_NOOP("HelpTexts", "Choose whether parts that have no number in the budget can be used "
                                     "freely."),
      QT_TRANSLATE_NOOP("HelpTexts", "Ticked, a part with no number in the budget can be used as often as "
                                     "you like. Unticked, it can’t be used at all until you give it a "
                                     "number.") },
    { "prefs.newTemplate", "",
      QT_TRANSLATE_NOOP("HelpTexts", "New layout template"),
      QT_TRANSLATE_NOOP("HelpTexts", "A layout that every new layout starts from."),
      QT_TRANSLATE_NOOP("HelpTexts", "Handy if your club always starts with the same baseplates or "
                                     "sheets. Leave it empty to start from a blank map.") },
    { "prefs.connectionPoints", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Connection points"),
      QT_TRANSLATE_NOOP("HelpTexts", "The small dots where track and other pieces join."),
      QT_TRANSLATE_NOOP("HelpTexts", "Normally they show only on the pieces you pick. Tick this to see "
                                     "them on every piece, which helps when joining long runs of track.") },
    { "prefs.partLibraries", "",
      QT_TRANSLATE_NOOP("HelpTexts", "More part folders"),
      QT_TRANSLATE_NOOP("HelpTexts", "Extra folders of parts to show in the Parts panel, like ones a "
                                     "friend shared."),
      QT_TRANSLATE_NOOP("HelpTexts", "Add a folder and its parts appear next to the built-in ones. "
                                     "Removing a folder here doesn’t delete anything.") },
    { "prefs.ldraw", "",
      QT_TRANSLATE_NOOP("HelpTexts", "LDraw library"),
      QT_TRANSLATE_NOOP("HelpTexts", "The LDraw parts, needed to bring in models from Studio, LDraw or "
                                     "LEGO Digital Designer."),
      QT_TRANSLATE_NOOP("HelpTexts", "If you don’t have it, press the download button and the app fetches "
                                     "it for you. You only need it for bringing in models.") },
    // Working with a server
    { "connect.server", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Server address"),
      QT_TRANSLATE_NOOP("HelpTexts", "The address of your club’s layout server, the same one you open in "
                                     "your browser."),
      QT_TRANSLATE_NOOP("HelpTexts", "Ask whoever runs your club’s server for it. You sign in once in "
                                     "your browser, and the app remembers you.") },
    { "servers.list", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Your servers"),
      QT_TRANSLATE_NOOP("HelpTexts", "Every layout server you use: your club’s, a friend’s, and so on."),
      QT_TRANSLATE_NOOP("HelpTexts", "Add a server by the web address you open in your browser, then sign "
                                     "in. Each server remembers your sign-in on its own, and its layouts "
                                     "and parts stay with it.") },
    { "servers.pick", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Pick a server"),
      QT_TRANSLATE_NOOP("HelpTexts", "Choose the server your layout is on. The ones you opened lately are "
                                     "listed under it."),
      QT_TRANSLATE_NOOP("HelpTexts", "Double-click a recent layout to open it straight away, or a server "
                                     "to see all its layouts. Manage Servers adds, renames or removes "
                                     "them.") },
    { "servers.main", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Main server"),
      QT_TRANSLATE_NOOP("HelpTexts", "Your settings follow your account on the Main server."),
      QT_TRANSLATE_NOOP("HelpTexts", "Theme, colours and the other settings are kept with your account "
                                     "there, so they are the same on every computer, whichever server "
                                     "you are working on.") },
    { "publish.owner", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Save to"),
      QT_TRANSLATE_NOOP("HelpTexts", "Who the layout belongs to on the server: you, or your club."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick your club to keep the layout with the club, not with one "
                                     "person. The owner decides who else can see or change it.") },
    { "compare.about", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Your offline changes"),
      QT_TRANSLATE_NOOP("HelpTexts", "What you changed while offline, next to what changed on the server."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick a row to see it on the map. Changes that don’t clash are "
                                     "simply added; for the ones that clash, choose what to keep.") },
    { "compare.keep", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Keep mine or the server’s"),
      QT_TRANSLATE_NOOP("HelpTexts", "For each clash, choose your version, the server’s, or both."),
      QT_TRANSLATE_NOOP("HelpTexts", "“Keep both” adds yours as a copy next to the server’s. “Later” "
                                     "keeps your changes waiting, so nothing is lost.") },
    { "upload.parts", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Upload your parts"),
      QT_TRANSLATE_NOOP("HelpTexts", "Parts you made yourself that the server doesn’t have yet."),
      QT_TRANSLATE_NOOP("HelpTexts", "Upload them so everyone who opens your layouts on the server sees "
                                     "them too. Pick your club as the owner to share them with the club.") },
};

// The desktop's own words for a few shared keys, where the web's would be
// wrong here. This is the only list of them: HelpTextsTest lets exactly
// these keys differ from the web. A null field keeps the web's words.
struct Override {
    const char* key;
    const char* title;
    const char* shortText;
    const char* more;
};

constexpr Override kDesktopOverrides[] = {
    // The desktop writes the pictures into a folder, not a zip file.
    { "share.exportAllViews", nullptr,
      QT_TRANSLATE_NOOP("HelpTexts", "One picture of every saved view, into a folder you choose."),
      nullptr },
    // The desktop also keeps modules as files, and changes a server module in the window itself.
    { "panel.moduleLibrary", nullptr, nullptr,
      QT_TRANSLATE_NOOP("HelpTexts", "This computer keeps modules as files in a folder. The server's tab has "
                                     "yours and your clubs', shared with everyone in the club, and Catalog has "
                                     "what people shared for everyone. Add to layout drops a copy in; to change "
                                     "a server module, pick ⋯ › Open to change it, then Save makes a new version.") },
};

HelpEntry translated(const Raw& r) {
    const auto tr = [](const char* s) { return QCoreApplication::translate("HelpTexts", s); };
    return { tr(r.title), tr(r.shortText), tr(r.more), QString::fromLatin1(r.learnMoreUrl) };
}

HelpEntry withOverride(HelpEntry e, const Override& o) {
    const auto tr = [](const char* s) { return QCoreApplication::translate("HelpTexts", s); };
    if (o.title) e.title = tr(o.title);
    if (o.shortText) e.shortText = tr(o.shortText);
    if (o.more) e.more = tr(o.more);
    return e;
}

}  // namespace

std::optional<HelpEntry> sharedHelpEntry(const QString& key) {
    for (const Raw& r : kShared)
        if (key == QLatin1String(r.key)) return translated(r);
    return std::nullopt;
}

std::optional<HelpEntry> helpEntry(const QString& key) {
    if (auto shared = sharedHelpEntry(key)) {
        for (const Override& o : kDesktopOverrides)
            if (key == QLatin1String(o.key)) return withOverride(*shared, o);
        return shared;
    }
    for (const Raw& r : kDesktopOnly)
        if (key == QLatin1String(r.key)) return translated(r);
    return std::nullopt;
}

QStringList sharedHelpKeys() {
    QStringList keys;
    for (const Raw& r : kShared) keys << QString::fromLatin1(r.key);
    return keys;
}

QStringList desktopOnlyHelpKeys() {
    QStringList keys;
    for (const Raw& r : kDesktopOnly) keys << QString::fromLatin1(r.key);
    return keys;
}

QStringList desktopOverriddenHelpKeys() {
    QStringList keys;
    for (const Override& o : kDesktopOverrides) keys << QString::fromLatin1(o.key);
    return keys;
}

QStringList helpKeys() { return sharedHelpKeys() + desktopOnlyHelpKeys(); }

}  // namespace bld::ui::help
