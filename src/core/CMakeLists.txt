add_library(bld_core STATIC
    Map.cpp
    Version.h
    Version.cpp
    Ids.h
    Geometry.h
    GridSnap.h
    Map.h
    Layer.h
    LayerItem.h
    LayerGrid.h
    LayerBrick.h
    LayerText.h
    LayerArea.h
    LayerRuler.h
    FontSpec.h
    ColorSpec.h
    Brick.h
    Group.h
    TextCell.h
    RulerItem.h
    AnchoredLabel.h
    Module.h
    ModuleLook.h
    ModuleEdit.h
    Venue.h
    Sidecar.h
    SavedView.h
)

target_include_directories(bld_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_core PUBLIC Qt6::Core Qt6::Gui)
