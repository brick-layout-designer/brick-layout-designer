#include "../parts/PartsLibrary.h"
#include "../ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileOpenEvent>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {

// Directory holding the compiled bld_<lang>.qm catalogues. Mirrors the
// install layouts in src/app/CMakeLists.txt (and the parts-library probe in
// MainWindow::defaultVendoredPartsRoot):
//   Windows / build tree:  <exeDir>/translations
//   macOS .app bundle:     Contents/MacOS/ -> ../Resources/translations
//   Linux / AppImage:      bin/ -> ../share/brick-layout-designer/translations
QString translationsDir() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    for (const QString& rel : {
             QStringLiteral("/translations"),
             QStringLiteral("/../Resources/translations"),
             QStringLiteral("/../share/brick-layout-designer/translations") }) {
        if (QDir(exeDir + rel).exists()) return QDir(exeDir + rel).absolutePath();
    }
    return exeDir + QStringLiteral("/translations");
}

// macOS delivers "Open With" / double-clicked documents as QFileOpenEvent
// rather than argv, both at launch and while the app is running.
class FileOpenFilter : public QObject {
public:
    explicit FileOpenFilter(bld::ui::MainWindow& window) : window_(window) {}

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent*>(ev)->file();
            if (!path.isEmpty()) window_.openFile(path);
            return true;
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    bld::ui::MainWindow& window_;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication::setOrganizationName(QStringLiteral("BrickLayoutDesigner"));
    QApplication::setApplicationName(QStringLiteral("Brick Layout Designer"));
    QApplication::setApplicationVersion(QStringLiteral(BLD_VERSION));

    QApplication app(argc, argv);

    // --version doubles as the packaging smoke test in CI: reaching it
    // proves the bundled Qt libraries and platform plugin all load.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cross-platform maintainer fork of BlueBrick"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("BlueBrick map (.bbm) to open."),
                                 QStringLiteral("[file]"));
    parser.process(app);

    // Load the user-selected UI language's bld_<code>.qm plus Qt's own
    // qtbase_<code>.qm. Deployed builds carry qtbase_*.qm next to ours
    // (windeployqt / macdeployqt); dev builds find it under the Qt install.
    static QTranslator appTranslator;
    static QTranslator qtTranslator;
    QString langCode = QSettings().value(QStringLiteral("general/language")).toString();
    if (langCode.isEmpty()) langCode = QLocale::system().name().split('_').value(0);
    // Norwegian systems report Bokmål (nb) or Nynorsk (nn); we ship "no" as BlueBrick did.
    if (langCode == QLatin1String("nb") || langCode == QLatin1String("nn")) langCode = QStringLiteral("no");
    if (!langCode.isEmpty() && langCode != QStringLiteral("en")) {
        const QString dir = translationsDir();
        if (appTranslator.load(QStringLiteral("bld_") + langCode, dir)) {
            QCoreApplication::installTranslator(&appTranslator);
        }
        // Qt's own catalogues name Norwegian "nb".
        const QString qtCode = langCode == QLatin1String("no") ? QStringLiteral("nb") : langCode;
        if (qtTranslator.load(QStringLiteral("qtbase_") + qtCode, dir)
            || qtTranslator.load(QStringLiteral("qtbase_") + qtCode,
                                 QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            QCoreApplication::installTranslator(&qtTranslator);
        }
    }

    bld::parts::PartsLibrary lib;
    bld::ui::MainWindow window(lib);
    FileOpenFilter fileOpenFilter(window);
    app.installEventFilter(&fileOpenFilter);
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) {
        window.openFile(files.first());
    } else {
        // No file argument: reopen whatever the user had open last session —
        // but first check for a crash-recovery autosave.
        const QString last = QSettings().value(QStringLiteral("recent/lastFile")).toString();
        const bool restored = window.restoreAutosaveIfAny(last);
        if (!restored && !last.isEmpty() && QFile::exists(last)) {
            window.openFile(last);
        }
    }
    // Guarantee a working canvas. If every load path above failed (first
    // run, missing lastFile, autosave declined), seed a blank doc so the
    // layer panel etc. operate on a real Map instead of silently no-op'ing.
    window.ensureDocument();

    return QApplication::exec();
}
