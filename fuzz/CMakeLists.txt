# libFuzzer harnesses for the file parsers (fuzz/README.md). Configure with
# clang and -DBLD_FUZZ=ON; the whole build is then instrumented.

set(BLD_FUZZ_TARGETS bbm sidecar venue part_xml budget ldraw_map tdl ncp ldraw_import lxfml studio)

foreach(t IN LISTS BLD_FUZZ_TARGETS)
    add_executable(fuzz_${t} FuzzTarget.cpp)
    target_compile_definitions(fuzz_${t} PRIVATE
        BLD_FUZZ_TARGET="${t}"
        BLD_PARTS_DIR="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts")
    target_link_libraries(fuzz_${t} PRIVATE bld_import bld_edit bld_saveload bld_parts bld_core Qt6::Gui)
    target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
endforeach()

add_executable(bld_fuzz_seeds SeedWriter.cpp)
target_link_libraries(bld_fuzz_seeds PRIVATE bld_edit bld_saveload bld_core Qt6::Core)
# QZipWriter (for a Studio .io seed) is private.
target_link_libraries(bld_fuzz_seeds PRIVATE Qt6::CorePrivate)

# The sync document reader (yrs), when built.
if(BLD_SYNC)
    add_executable(fuzz_ydoc YDocFuzz.cpp)
    target_link_libraries(fuzz_ydoc PRIVATE bld_sync)
    target_link_options(fuzz_ydoc PRIVATE -fsanitize=fuzzer)
    add_executable(fuzz_sync_message SyncMessageFuzz.cpp)
    target_link_libraries(fuzz_sync_message PRIVATE bld_sync)
    target_link_options(fuzz_sync_message PRIVATE -fsanitize=fuzzer)
endif()
