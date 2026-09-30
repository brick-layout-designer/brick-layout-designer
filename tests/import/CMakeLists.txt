add_executable(bld_import_tests
    ArchivePathTest.cpp
    GifWriterTest.cpp
    LDDConversionTest.cpp
    ImportPipelineTest.cpp
    LDrawReaderTest.cpp
    LDrawLibraryTest.cpp
    LDrawMeshLoaderTest.cpp
    LDrawPaletteTest.cpp
    LifReaderTest.cpp
    MeshRasterizeTest.cpp
    SafeZipTest.cpp
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

# Studio/LDD tests build synthetic ZIPs with Qt's private QZipWriter.
target_link_libraries(bld_import_tests PRIVATE Qt6::CorePrivate)

target_compile_definitions(bld_import_tests PRIVATE BLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

gtest_discover_tests(bld_import_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DISCOVERY_TIMEOUT 60
)
