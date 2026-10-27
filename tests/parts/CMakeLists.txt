add_executable(bld_parts_tests
    PartsLibraryTest.cpp
    SetVersionTest.cpp
)

target_compile_definitions(bld_parts_tests PRIVATE
    BLD_PARTS_LIBRARY_ROOT="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
)

target_link_libraries(bld_parts_tests
    PRIVATE
        bld_parts
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
)

gtest_discover_tests(bld_parts_tests
    DISCOVERY_TIMEOUT 60
)
