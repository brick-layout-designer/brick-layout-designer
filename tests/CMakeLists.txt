include(FetchContent)

FetchContent_Declare(
    googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
# Enumerate tests when ctest runs rather than as a post-build step, so the
# build doesn't need the Qt DLLs on PATH (Windows) or a sanitizer-clean
# discovery run to succeed.
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

add_subdirectory(saveload)
add_subdirectory(parts)
add_subdirectory(rendering)
add_subdirectory(edit)
add_subdirectory(import)
add_subdirectory(sync)
add_subdirectory(ui)

# ----- End-to-end binaries ------------------------------------------------------
# The shipped GUI binary starts, constructs QApplication and reports the
# configured version (the same check CI runs against every package).
add_test(NAME App.Version COMMAND bld_app --version)
set_tests_properties(App.Version PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=1"
    PASS_REGULAR_EXPRESSION "Brick Layout Designer ${PROJECT_VERSION}")

# Headless load + render of a real corpus map through the CLI.
if(EXISTS "${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus/tight-corner.bbm")
    add_test(NAME Cli.RenderTightCorner
        COMMAND bld_render
                "${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus/tight-corner.bbm"
                "${CMAKE_CURRENT_BINARY_DIR}/cli-render-tight-corner.png"
                800
                "${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts")
    set_tests_properties(Cli.RenderTightCorner PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        PASS_REGULAR_EXPRESSION "Rendered .* \\(800x[0-9]+\\)")
endif()
