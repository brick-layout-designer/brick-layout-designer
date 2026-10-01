add_library(bld_rendering STATIC
    SceneBuilder.cpp
    SceneBuilder.h
    SceneBuilderInternal.h
    SceneBuilderElectric.cpp
    SceneBuilderSidecar.cpp
    ModuleLabels.cpp
    ModuleLabels.h
    MapText.cpp
    MapText.h
    VenueDraw.cpp
    VenueDraw.h
    MapScene.h
)

target_include_directories(bld_rendering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_rendering
    PUBLIC
        bld_core
        bld_parts
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

# The one font the map's text is drawn in, on every machine, the same as
# the web's (Liberation Sans, SIL OFL 1.1, see fonts/LICENSE).
qt_add_resources(bld_rendering "bld_map_fonts"
    PREFIX "/bld"
    FILES
        fonts/LiberationSans-Regular.ttf
        fonts/LiberationSans-Bold.ttf
        fonts/LiberationSans-Italic.ttf
        fonts/LiberationSans-BoldItalic.ttf
)
