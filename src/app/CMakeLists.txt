qt_add_executable(bld_app
    main.cpp
)

target_link_libraries(bld_app PRIVATE bld_ui)
target_compile_definitions(bld_app PRIVATE BLD_VERSION="${PROJECT_VERSION}")

set_target_properties(bld_app PROPERTIES
    OUTPUT_NAME "brick-layout-designer"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

set(_bld_icons "${PROJECT_SOURCE_DIR}/packaging/icons")

if(WIN32)
    # Exe icon + VERSIONINFO (Explorer "Details" tab, SmartScreen, MSI).
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/brick-layout-designer.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/brick-layout-designer.rc" @ONLY)
    target_sources(bld_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/brick-layout-designer.rc")
endif()

if(APPLE)
    # Build the .icns from the 256 px PNG with the stock macOS tools.
    set(_bld_icns "${CMAKE_CURRENT_BINARY_DIR}/brick-layout-designer.icns")
    add_custom_command(
        OUTPUT "${_bld_icns}"
        COMMAND "${CMAKE_COMMAND}"
                -DSRC=${_bld_icons}/brick-layout-designer.png
                -DOUT=${_bld_icns}
                -P "${PROJECT_SOURCE_DIR}/packaging/macos/make-icns.cmake"
        DEPENDS "${_bld_icons}/brick-layout-designer.png"
                "${PROJECT_SOURCE_DIR}/packaging/macos/make-icns.cmake"
        VERBATIM)
    target_sources(bld_app PRIVATE "${_bld_icns}")
    set_source_files_properties("${_bld_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(bld_app PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Brick Layout Designer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.brick-layout-designer"
        MACOSX_BUNDLE_ICON_FILE "brick-layout-designer.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "GPL-3.0; derivative of BlueBrick (c) Alban Nanty and contributors")
endif()

install(TARGETS bld_app
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION "${BLD_INSTALL_BINDIR}")

if(BLD_DEPLOY_QT)
    # Runs windeployqt / macdeployqt / Qt's Linux deployer against the
    # installed binary so the install tree runs without a Qt install.
    qt_generate_deploy_app_script(
        TARGET bld_app
        OUTPUT_SCRIPT _bld_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${_bld_deploy_script}")

    if(WIN32)
        # MSVC runtime DLLs app-local, so the zip runs on machines without
        # the VC++ redistributable installed.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${BLD_INSTALL_BINDIR}")
        include(InstallRequiredSystemLibraries)
    endif()
endif()

# Drop the Qt runtime next to the built .exe so double-clicking the
# dev binary works without putting Qt's bin/ on PATH. windeployqt is
# idempotent and incremental, so re-running it on every build is fine
# (and matches what packaging does in CI).
if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET bld_app POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-compiler-runtime
                    "$<TARGET_FILE:bld_app>"
            VERBATIM)
    endif()
endif()

# Headless CLI for dumping .bbm structure without requiring a GUI.
add_executable(bld_inspect bld_inspect.cpp)
target_link_libraries(bld_inspect PRIVATE bld_saveload bld_core Qt6::Core)

# Headless CLI that renders a .bbm to a PNG (uses QT_QPA_PLATFORM=offscreen).
add_executable(bld_render bld_render.cpp)
target_link_libraries(bld_render
    PRIVATE bld_rendering bld_saveload bld_parts bld_core
            Qt6::Core Qt6::Gui Qt6::Widgets)

# LDraw .ldr/.dat/.mpd -> .bbm importer.
add_executable(bld_ldraw_import bld_ldraw_import.cpp)
target_link_libraries(bld_ldraw_import
    PRIVATE bld_import bld_saveload bld_core Qt6::Core)

# External format (.ldr / .io / .lxf / .lxfml) -> top-down sprite PNG/GIF.
# Wraps the same render pipeline the UI's Tools → Import uses so CI
# pipelines and batch conversions can reach the sprite output without
# launching the desktop app.
add_executable(bld_sprite_gen bld_sprite_gen.cpp)
target_link_libraries(bld_sprite_gen
    PRIVATE bld_import bld_rendering bld_edit bld_parts bld_core
            Qt6::Core Qt6::Gui Qt6::Widgets)
