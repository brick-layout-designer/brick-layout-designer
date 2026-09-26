cmake_minimum_required(VERSION 3.25)

# Single source of truth for the version stamped into the binaries and every
# package. Release CI overrides it from the git tag (-DBLD_VERSION=1.2.3).
set(BLD_VERSION "1.2.0" CACHE STRING "Version stamped into binaries and packages")

project(BrickLayoutDesigner
    VERSION ${BLD_VERSION}
    DESCRIPTION "Cross-platform maintainer fork of BlueBrick"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport)
find_package(Qt6 6.7 QUIET COMPONENTS LinguistTools)
qt_standard_project_setup()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(WIN32)
    # Flat layout: Qt's deploy script puts the Qt DLLs and qt.conf in
    # CMAKE_INSTALL_BINDIR, so it must be the exe's own folder.
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executable install directory")
endif()
include(GNUInstallDirs)

# ----- Install layout ---------------------------------------------------------
# `cmake --install` produces a runnable tree on every platform; CI packages it
# as-is. The runtime probes in src/app/main.cpp (translations) and
# MainWindow::defaultVendoredPartsRoot (parts library) mirror these paths.
#   Windows: <prefix>/brick-layout-designer.exe, <prefix>/{parts,translations}
#   macOS:   <prefix>/brick-layout-designer.app, Contents/Resources/{parts,translations}
#   Linux:   <prefix>/bin, <prefix>/share/brick-layout-designer/{parts,translations}
if(WIN32)
    set(BLD_INSTALL_BINDIR  ".")
    set(BLD_INSTALL_DATADIR ".")
elseif(APPLE)
    set(BLD_INSTALL_BINDIR  ".")
    set(BLD_INSTALL_DATADIR "brick-layout-designer.app/Contents/Resources")
else()
    set(BLD_INSTALL_BINDIR  "${CMAKE_INSTALL_BINDIR}")
    set(BLD_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/brick-layout-designer")
endif()

# Bundle the Qt runtime (libs + plugins + qt.conf + MSVC runtime) into the
# install tree via windeployqt / macdeployqt. Linux bundling is done by
# linuxdeploy in CI instead: Qt's generic Linux deployer also sweeps in glibc
# and the dynamic loader, which breaks the result on other distros.
if(WIN32 OR APPLE)
    option(BLD_DEPLOY_QT "Bundle the Qt runtime into the install tree" ON)
endif()
option(BLD_INSTALL_PARTS "Install the vendored BlueBrickParts library" ON)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ----- Localization -----------------------------------------------------------
# Base English is implicit (Qt returns the tr() argument when no catalogue is
# loaded). Supported target languages match BlueBrick's shipped set plus a
# few we want to cover going forward. Translators populate the .ts files via
# `lupdate`; `lrelease` compiles them to .qm which the build installs next to
# the binary. See docs/TRANSLATING.md.
set(BLD_LANGUAGES fr de es it nl pt zh ja)

set(BLD_TS_FILES "")
foreach(_lang IN LISTS BLD_LANGUAGES)
    list(APPEND BLD_TS_FILES "${CMAKE_SOURCE_DIR}/translations/bld_${_lang}.ts")
endforeach()

# Only wire the binaries when every .ts file exists so a fresh clone before
# any translations are stubbed out still builds.
set(BLD_HAVE_TS TRUE)
foreach(_ts IN LISTS BLD_TS_FILES)
    if(NOT EXISTS "${_ts}")
        set(BLD_HAVE_TS FALSE)
    endif()
endforeach()
if(BLD_HAVE_TS AND TARGET bld_app AND Qt6LinguistTools_FOUND)
    qt_add_translations(bld_app
        TS_FILES ${BLD_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE BLD_QM_FILES)
    install(FILES ${BLD_QM_FILES} DESTINATION "${BLD_INSTALL_DATADIR}/translations" OPTIONAL)
endif()

# ----- Data files -------------------------------------------------------------
if(BLD_INSTALL_PARTS AND EXISTS "${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
            DESTINATION "${BLD_INSTALL_DATADIR}/parts/BlueBrickParts")
    install(FILES "${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/README.md"
            DESTINATION "${BLD_INSTALL_DATADIR}/parts/BlueBrickParts")
endif()

if(WIN32)
    install(FILES README.md DESTINATION .)
    install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
elseif(APPLE)
    install(FILES README.md LICENSE DESTINATION "${BLD_INSTALL_DATADIR}")
else()
    install(FILES README.md LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
    install(FILES packaging/linux/brick-layout-designer.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES packaging/linux/brick-layout-designer.xml
            DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
    install(FILES packaging/icons/brick-layout-designer.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps")
endif()
