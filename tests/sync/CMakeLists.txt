add_executable(bld_sync_tests
    YDocSummaryTest.cpp
    UpdateGuardTest.cpp
    CompatTest.cpp
    WebModelTest.cpp
    SyncDocTest.cpp
    SyncProtocolTest.cpp
    SyncClientTest.cpp
    ServerApiTest.cpp
    SyncSessionTest.cpp
    AwarenessTest.cpp
    PartsUploadTest.cpp
    PartsSyncTest.cpp
    ConnectDialogTest.cpp
    ServerListTest.cpp
    ServersDialogTest.cpp
    RefreshOnFocusTest.cpp
    LiveLayoutTest.cpp
    LayoutMergeTest.cpp
    CompareDialogTest.cpp
    MainWindowLiveTest.cpp
    LayoutSourceTest.cpp
    PrefsSyncTest.cpp
    ServerLibraryTest.cpp
    SyncTestMain.cpp
)
target_compile_definitions(bld_sync_tests PRIVATE BLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}" BLD_TEST_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(bld_sync_tests PRIVATE bld_ui bld_sync_ui bld_sync bld_saveload bld_core GTest::gtest Qt6::Core Qt6::Widgets)
gtest_discover_tests(bld_sync_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS sync ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
