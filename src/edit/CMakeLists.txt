add_library(bld_edit STATIC
    EditCommands.cpp
    EditCommands.h
    ModuleCommands.cpp
    ModuleCommands.h
    LabelCommands.cpp
    LabelCommands.h
    TextCommands.cpp
    TextCommands.h
    LayerCommands.cpp
    LayerCommands.h
    AreaCommands.cpp
    AreaCommands.h
    RulerCommands.cpp
    RulerCommands.h
    VenueCommands.cpp
    VenueCommands.h
    VenueValidator.cpp
    VenueValidator.h
    venue/VenueUnits.cpp
    venue/VenueUnits.h
    venue/VenueDesign.cpp
    venue/VenueDesign.h
    venue/VenueDesignerState.cpp
    venue/VenueDesignerState.h
    Connectivity.cpp
    PartList.cpp
    PartList.h
    FlexMove.cpp
    FlexMove.h
    Connectivity.h
    Budget.cpp
    Budget.h
)

target_include_directories(bld_edit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_edit
    PUBLIC
        bld_core
        bld_parts
        Qt6::Core
        Qt6::Widgets     # QUndoCommand lives in QtWidgets
)
