add_library(bld_parts STATIC
    BrickPlacement.h
    PartsLibrary.cpp
    PartsLibrary.h
)

target_include_directories(bld_parts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_parts PUBLIC Qt6::Core Qt6::Gui)
