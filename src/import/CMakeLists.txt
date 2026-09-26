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

    # Zip-slip guard shared by every archive extractor.
    ArchivePath.cpp
    ArchivePath.h
)

target_include_directories(bld_import PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# StudioReader uses QZipReader from Qt's private Core headers. The public
# Qt6::Core target doesn't expose the private include path, so we pull it
# in explicitly. Using private Qt APIs is a stability tradeoff — it can
# break across Qt minor versions, but it spares us the external QuaZip /
# miniz dep for a tiny feature.
target_link_libraries(bld_import PUBLIC bld_core bld_geom bld_parts bld_edit Qt6::Core Qt6::Gui)
# Qt 6 layout puts private headers under
# <prefix>/include/QtCore/<version>/QtCore/private/. Locate the specific
# directory that contains the header we need, then add its parent so
# `#include <private/qzipreader_p.h>` resolves.
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
if(BLD_QZIPREADER_INCLUDE_DIR)
    target_include_directories(bld_import PRIVATE "${BLD_QZIPREADER_INCLUDE_DIR}")
    # The private headers also reference <QtCore/private/qglobal_p.h>
    # etc., which resolves via the version-parent (one directory up).
    get_filename_component(_qtcore_version_parent
        "${BLD_QZIPREADER_INCLUDE_DIR}" DIRECTORY)
    target_include_directories(bld_import PRIVATE "${_qtcore_version_parent}")
    message(STATUS "bld_import: QZipReader private headers at ${BLD_QZIPREADER_INCLUDE_DIR}")
else()
    message(WARNING
        "bld_import: qzipreader_p.h not found — Studio .io import will be stubbed. "
        "Install the Qt private headers package if you want this feature.")
    target_compile_definitions(bld_import PRIVATE BLD_NO_QZIPREADER=1)
endif()
