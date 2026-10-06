add_executable(bld_rendering_tests
    RenderSmokeTest.cpp
    RenderGoldensTest.cpp
    VenueDrawTest.cpp
    CircuitPathTest.cpp
)

target_compile_definitions(bld_rendering_tests PRIVATE
    BLD_BBM_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus"
    BLD_PARTS_LIBRARY_ROOT="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
    BLD_RENDER_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/fixtures/render-goldens"
    BLD_CIRCUIT_FIXTURE="${CMAKE_SOURCE_DIR}/fixtures/electric/circuit-paths.json"
    BLD_VENUE_FIXTURE="${CMAKE_SOURCE_DIR}/fixtures/venues/grand-lobby.bld-venue"
)

target_link_libraries(bld_rendering_tests
    PRIVATE
        bld_rendering
        bld_saveload
        bld_parts
        bld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

gtest_discover_tests(bld_rendering_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DISCOVERY_TIMEOUT 60
)
