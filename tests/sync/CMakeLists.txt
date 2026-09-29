add_executable(bld_sync_tests
    YDocSummaryTest.cpp
    WebModelTest.cpp
)
target_compile_definitions(bld_sync_tests PRIVATE BLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(bld_sync_tests PRIVATE bld_sync bld_saveload bld_core GTest::gtest_main Qt6::Core)
gtest_discover_tests(bld_sync_tests DISCOVERY_TIMEOUT 60)
