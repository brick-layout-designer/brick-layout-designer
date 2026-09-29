add_library(bld_saveload STATIC
    XmlPrimitives.cpp
    XmlPrimitives.h
    XmlCanonicalWriter.cpp
    XmlCanonicalWriter.h
    LayerIO.cpp
    LayerIO.h
    BbmReader.cpp
    BbmReader.h
    BbmWriter.cpp
    BbmWriter.h
    SidecarIO.cpp
    SidecarIO.h
    VenueIO.cpp
    VenueIO.h
    VenueJson.cpp
    VenueJson.h
    SetIO.cpp
    SetIO.h
)

target_link_libraries(bld_saveload PUBLIC bld_core Qt6::Core Qt6::Gui)
