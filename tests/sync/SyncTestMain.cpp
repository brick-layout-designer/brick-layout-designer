// The sync tests need a Qt event loop (SyncClient's WebSockets), and the
// sync screens a QApplication.

#include <gtest/gtest.h>

#include <QApplication>

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
    return RUN_ALL_TESTS();
}
