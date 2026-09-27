add_executable(bld_ui_tests
    MapViewTest.cpp
    ImportPipelineTest.cpp
    LDrawMapTest.cpp
    BrickPlacementTest.cpp
    ConnectivityTest.cpp
    FlexMoveTest.cpp
)

target_compile_definitions(bld_ui_tests PRIVATE
    BLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    BLD_BBM_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus"
    BLD_PARTS_LIBRARY_ROOT="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
)

target_link_libraries(bld_ui_tests
    PRIVATE
        bld_ui
        bld_import
        bld_edit
        bld_saveload
        bld_parts
        bld_core
        GTest::gtest
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

gtest_discover_tests(bld_ui_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DISCOVERY_TIMEOUT 60
)
