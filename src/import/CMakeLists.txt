add_library(bld_import STATIC
    # LDraw / Studio shared pipeline (parser, library scanner, palette,
    # subfile mesh resolver, top-down rasterizer, BlueBrick-format
    # rasterizer fallback for inline primitives).
    ldraw/LDrawReader.cpp
    ldraw/LDrawReader.h
    ldraw/LDrawColors.cpp
    ldraw/LDrawColors.h
    ldraw/LDrawLibrary.cpp
    ldraw/LDrawLibrary.h
    ldraw/LDrawMeshBuilder.cpp
    ldraw/LDrawMeshBuilder.h
    ldraw/LDrawMeshLoader.cpp
    ldraw/LDrawMeshLoader.h
    ldraw/LDrawPalette.cpp
    ldraw/LDrawPalette.h
    ldraw/LDrawRasterize.cpp
    ldraw/LDrawRasterize.h

    # Studio (.io) — wraps an LDraw model.ldr in a zip alongside thumbs.
    studio/StudioReader.cpp
    studio/StudioReader.h

    # LDD (LEGO Digital Designer) — LXFML reader, .g binary geometry
    # reader, and the ldraw.xml rosetta-stone for mapping LDD design
    # IDs to LDraw equivalents.
    ldd/LDDReader.cpp
    ldd/LDDReader.h
    ldd/LDDGeomReader.cpp
    ldd/LDDGeomReader.h
    ldd/LDDLDrawMapping.cpp
    ldd/LDDLDrawMapping.h
    ldd/LDDAssets.cpp
    ldd/LDDAssets.h
    ldd/LDDMaterials.cpp
    ldd/LDDMaterials.h
    ldd/LDDMeshBuilder.cpp
    ldd/LDDMeshBuilder.h

    # LIF (LEGO Interchange Format) — LDD's packed asset archive.
    lif/LifReader.cpp
    lif/LifReader.h

    # Format-agnostic top-down rasterizer for bld::geom::Mesh.
    mesh/MeshRasterize.cpp
    mesh/MeshRasterize.h

    # Format-agnostic emission of a BlueBrick library part (sprite +
    # XML metadata + ConnexionList) — used by every importer above as
    # the final step.
    ImportToPart.cpp
    ImportToPart.h

    # GIF89a encoder (Qt only reads GIF) for vanilla-loadable sprites.
    GifWriter.cpp
    GifWriter.h

    # Free connection points of an imported model -> part ConnexionList.
    ImportConnections.cpp
    ImportConnections.h

    # Other layout programs' map formats (BlueBrick-compatible).
    mapformats/FourDBrixMap.cpp
    mapformats/FourDBrixMap.h
    mapformats/LDrawMap.cpp
    mapformats/LDrawMap.h
    mapformats/TrackDesignerMap.cpp
    mapformats/TrackDesignerMap.h

    # Zip-slip guard shared by every archive extractor.
    ArchivePath.cpp
    zip/SafeZip.cpp
    zip/SafeZip.h
    zip/puff.c
    zip/puff.h
    ArchivePath.h
)

target_include_directories(bld_import PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_import PUBLIC bld_core bld_geom bld_parts bld_edit bld_saveload Qt6::Core Qt6::Gui)
# Tests and fuzz seeds write ZIPs with Qt's private QZipWriter. Qt 6 puts
# private headers under <prefix>/include/QtCore/<version>/QtCore/private/;
# locate that directory (its parent is added too, for qglobal_p.h etc.).
find_path(BLD_QZIPREADER_INCLUDE_DIR
    NAMES private/qzipreader_p.h
    PATHS
        # Start from the public Qt Core include dir and walk into the
        # version-numbered sibling. The find_path search walks sub-
        # directories automatically; NO_DEFAULT_PATH keeps us from
        # pulling in unrelated system headers.
        /usr/include/qt6/QtCore/${Qt6Core_VERSION}/QtCore
        /usr/include/qt6/QtCore
    NO_DEFAULT_PATH)
if(NOT BLD_QZIPREADER_INCLUDE_DIR)
    # Generic fallback: search every versioned sub-dir under the Core
    # include path. Reasonable on macOS / Windows Qt installer layouts.
    get_target_property(_qt_core_includes Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_inc IN LISTS _qt_core_includes)
        file(GLOB _candidates LIST_DIRECTORIES true "${_inc}/*/QtCore")
        foreach(_c IN LISTS _candidates)
            if(EXISTS "${_c}/private/qzipreader_p.h")
                set(BLD_QZIPREADER_INCLUDE_DIR "${_c}" CACHE INTERNAL "")
                break()
            endif()
        endforeach()
        if(BLD_QZIPREADER_INCLUDE_DIR)
            break()
        endif()
    endforeach()
endif()
# ZIPs are read with zip/SafeZip; the private QZipWriter found above is
# only for tests and fuzz seeds that write archives.
if(BLD_QZIPREADER_INCLUDE_DIR)
    message(STATUS "QZipWriter private headers at ${BLD_QZIPREADER_INCLUDE_DIR}")
else()
    message(STATUS "Qt private headers not found: tests that write ZIP archives are skipped")
endif()
