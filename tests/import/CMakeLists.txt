add_executable(bld_import_tests
    ArchivePathTest.cpp
    GifWriterTest.cpp
    ImportPipelineTest.cpp
    LDrawReaderTest.cpp
    LDrawLibraryTest.cpp
    LDrawMeshLoaderTest.cpp
    LDrawPaletteTest.cpp
    LifReaderTest.cpp
    MeshRasterizeTest.cpp
    StudioReaderTest.cpp
    LDDGeomReaderTest.cpp
    LDDLDrawMappingTest.cpp
    LDDMaterialsTest.cpp
    LDDReaderTest.cpp
    LDrawRasterizeTest.cpp
)

target_link_libraries(bld_import_tests
    PRIVATE
        bld_import
        bld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
)

# Studio/LDD tests construct synthetic ZIPs in temp dirs using Qt's
# private QZipWriter — same approach the reader side uses. bld_import's
# CMakeLists.txt already located the private header dir and cached it
# in BLD_QZIPREADER_INCLUDE_DIR; reuse it here so the tests can include
# <private/qzipwriter_p.h>.
if(BLD_QZIPREADER_INCLUDE_DIR)
    target_include_directories(bld_import_tests PRIVATE
        "${BLD_QZIPREADER_INCLUDE_DIR}")
    get_filename_component(_qtcore_private_parent
        "${BLD_QZIPREADER_INCLUDE_DIR}" DIRECTORY)
    target_include_directories(bld_import_tests PRIVATE
        "${_qtcore_private_parent}")
endif()

gtest_discover_tests(bld_import_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DISCOVERY_TIMEOUT 60
)
