// The sync tests need a Qt event loop (SyncClient's WebSockets), and the
// sync screens a QApplication.

#include <gtest/gtest.h>

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>

namespace {

// Every test starts from empty settings. Settings live on disk, so what one
// test left behind (a Module library folder in a deleted temp dir) used to
// reach the tests after it, in this run and the next: run after the Server
// window tests, MainWindowLiveMyPart looked for my parts in that folder,
// found none and never fetched the server's catalog. CI passed only because
// it starts with no settings and runs those tests first.
class FreshSettings : public ::testing::EmptyTestEventListener {
    void OnTestStart(const ::testing::TestInfo&) override {
        // Both stores: the fixtures turn QStandardPaths' test mode on and off.
        const bool was = QStandardPaths::isTestModeEnabled();
        for (const bool testMode : { false, true }) {
            QStandardPaths::setTestModeEnabled(testMode);
            QSettings().clear();
        }
        QStandardPaths::setTestModeEnabled(was);
    }
};

}  // namespace

int main(int argc, char** argv) {
    // Run off screen as ctest and CI do, also when started by hand from a
    // desktop session: a real compositor (Wayland) may refuse the popups and
    // focus the tests drive.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // Keep QSettings (the last server address) away from the real app's store.
    QCoreApplication::setOrganizationName(QStringLiteral("BrickLayoutDesignerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("bld_sync_tests"));
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new FreshSettings);
    return RUN_ALL_TESTS();
}
