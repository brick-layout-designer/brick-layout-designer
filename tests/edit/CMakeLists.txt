add_executable(bld_edit_tests
    EditCommandsTest.cpp
    ModuleCommandsTest.cpp
    ModuleLibraryLinkTest.cpp
    LabelCommandsTest.cpp
    LayerCommandsTest.cpp
    AreaCommandsTest.cpp
    BudgetTest.cpp
    PartListTest.cpp
    VenueDesignerTest.cpp
    SetsTest.cpp
)

target_compile_definitions(bld_edit_tests PRIVATE BLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(bld_edit_tests
    PRIVATE
        bld_edit
        bld_saveload
        bld_parts
        bld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Widgets
)

gtest_discover_tests(bld_edit_tests
    DISCOVERY_TIMEOUT 60
)
