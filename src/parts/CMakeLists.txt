add_library(bld_parts STATIC
    BrickPlacement.h
    ColorNames.cpp
    ColorNames.h
    PartsLibrary.cpp
    PartsLibrary.h
)

target_include_directories(bld_parts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_parts PUBLIC Qt6::Core Qt6::Gui)

# BlueBrick's color names (config/ColorTable.xml from upstream, GPL-3).
qt_add_resources(bld_parts "bld_parts_data"
    PREFIX "/bld"
    BASE data
    FILES data/ColorTable.xml
)
