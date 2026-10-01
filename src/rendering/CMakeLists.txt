add_library(bld_rendering STATIC
    SceneBuilder.cpp
    SceneBuilder.h
    SceneBuilderInternal.h
    SceneBuilderElectric.cpp
    SceneBuilderSidecar.cpp
    ModuleLabels.cpp
    ModuleLabels.h
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
