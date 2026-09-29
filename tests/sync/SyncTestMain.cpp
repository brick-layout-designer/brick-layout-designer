// The sync tests need a Qt event loop (SyncClient's WebSockets), and the
// sync screens a QApplication.

#include <gtest/gtest.h>

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Keep QSettings (the last server address) away from the real app's store.
    QCoreApplication::setOrganizationName(QStringLiteral("BrickLayoutDesignerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("bld_sync_tests"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
