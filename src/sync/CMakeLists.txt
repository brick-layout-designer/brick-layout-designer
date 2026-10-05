# Live sync with the collaborative web server (docs: DESKTOP-LIVE-SYNC in the
# web repo). Phase P2 spike: yrs (Rust Yjs) linked through its C API, yffi,
# plus Qt WebSockets, on every platform. Always built; needs a current stable
# Rust toolchain (cargo, checked in the top-level CMakeLists.txt).

include(FetchContent)

FetchContent_Declare(
    Corrosion
    URL      https://github.com/corrosion-rs/corrosion/archive/refs/tags/v0.6.1.tar.gz
    URL_HASH SHA256=e9e95b1ee2bad52681f347993fb1a5af5cce458c5ce8a2636c9e476e4babf8e3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Corrosion)

# yffi builds only a C library (staticlib / cdylib), so it can't be a
# dependency of a Rust crate of ours: build the published crate itself,
# with the Cargo.lock it ships. third_party/yrs/libyrs.h is cbindgen's header for
# exactly this version: bump both together (scripts/sync-fixtures/regen-libyrs-header.sh).
FetchContent_Declare(
    yffi
    URL           https://static.crates.io/crates/yffi/yffi-0.28.0.crate
    URL_HASH      SHA256=ad9793055d5d7307d372438728cd7306b80dd4458348bdb654fde41397d653b7
    DOWNLOAD_NAME yffi-0.28.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(yffi)
list(LENGTH CMAKE_OSX_ARCHITECTURES _bld_osx_archs)
if(APPLE AND _bld_osx_archs GREATER 1)
    # A universal macOS app: Corrosion 0.6 builds one Rust target, so build
    # yffi for each architecture with cargo and lipo them into one library.
    set(_bld_cargo_targets "")
    set(_bld_yrs_libs "")
    foreach(_arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        if(_arch STREQUAL "arm64")
            set(_triple aarch64-apple-darwin)
        elseif(_arch STREQUAL "x86_64")
            set(_triple x86_64-apple-darwin)
        else()
            message(FATAL_ERROR "No Rust target for macOS architecture ${_arch}")
        endif()
        list(APPEND _bld_cargo_targets --target ${_triple})
        list(APPEND _bld_yrs_libs "${CMAKE_CURRENT_BINARY_DIR}/yffi-target/${_triple}/release/libyrs.a")
    endforeach()
    set(_bld_yrs_universal "${CMAKE_CURRENT_BINARY_DIR}/libyrs-universal.a")
    set(_bld_cargo_env "")
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(_bld_cargo_env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    add_custom_command(
        OUTPUT "${_bld_yrs_universal}"
        COMMAND ${CMAKE_COMMAND} -E env ${_bld_cargo_env}
                "${BLD_CARGO}" build --release --locked --lib
                --manifest-path "${yffi_SOURCE_DIR}/Cargo.toml"
                --target-dir "${CMAKE_CURRENT_BINARY_DIR}/yffi-target"
                ${_bld_cargo_targets}
        COMMAND lipo -create ${_bld_yrs_libs} -output "${_bld_yrs_universal}"
        DEPENDS "${yffi_SOURCE_DIR}/Cargo.toml"
        COMMENT "Building yffi (Rust) for ${CMAKE_OSX_ARCHITECTURES}"
        VERBATIM)
    add_custom_target(yrs_universal DEPENDS "${_bld_yrs_universal}")
    add_library(yrs STATIC IMPORTED GLOBAL)
    set_target_properties(yrs PROPERTIES IMPORTED_LOCATION "${_bld_yrs_universal}")
    add_dependencies(yrs yrs_universal)
else()
    corrosion_import_crate(
        MANIFEST_PATH "${yffi_SOURCE_DIR}/Cargo.toml"
        CRATE_TYPES staticlib
        LOCKED
        PROFILE release
    )
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS WebSockets Widgets)

# QtKeychain keeps the server sign-in token in the OS keychain (macOS
# Keychain, Windows Credential Store, Secret Service on Linux). Built
# static from the pinned release; on Linux it needs libsecret-1.
set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
set(_bld_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
# Not its own tests: they need a keychain, which CI machines lack.
set(_bld_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_Declare(
    qtkeychain
    URL      https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/0.15.0.tar.gz
    URL_HASH SHA256=f4254dc8f0933b06d90672d683eab08ef770acd8336e44dfa030ce041dc2ca22
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(qtkeychain)
set(BUILD_SHARED_LIBS ${_bld_shared})
set(BUILD_TESTING ${_bld_testing})

add_library(bld_sync STATIC
    YDocSummary.h
    YDocSummary.cpp
    DocJson.h
    DocJson.cpp
    WebModel.h
    WebModel.cpp
    WebModelWriter.h
    WebModelWriter.cpp
    DocJsonDetail.h
    SyncDoc.h
    SyncDoc.cpp
    SyncProtocol.h
    SyncProtocol.cpp
    SyncClient.h
    SyncClient.cpp
    ServerApi.h
    ServerApi.cpp
    Compat.h
    Compat.cpp
    ServerRefusal.h
    ServerRefusal.cpp
    SyncSession.h
    SyncSession.cpp
    Awareness.h
    Awareness.cpp
    Presence.h
    Presence.cpp
    PartsUpload.h
    PartsUpload.cpp
    PartsSync.h
    PartsSync.cpp
    TokenStore.h
    TokenStore.cpp
    ServerList.h
    ServerList.cpp
    LayoutMerge.h
    LayoutMerge.cpp
    LibraryApi.h
    LibraryApi.cpp
    ModuleDoc.h
    ModuleDoc.cpp
    EventStream.h
    EventStream.cpp
)
target_include_directories(bld_sync
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/yrs)
# `yrs` is yffi's library target (its [lib] name).
target_link_libraries(bld_sync PUBLIC bld_core bld_saveload Qt6::Core Qt6::Gui Qt6::Network Qt6::WebSockets PRIVATE yrs qt6keychain)

# "Connect to Server…" and the other sync screens (Qt Widgets).
add_library(bld_sync_ui STATIC
    ui/ConnectDialog.h
    ui/ConnectDialog.cpp
    ui/ServersDialog.h
    ui/ServersDialog.cpp
    ui/UploadPartsDialog.h
    ui/UploadPartsDialog.cpp
    ui/CompareDialog.h
    ui/CompareDialog.cpp
    ui/RefreshOnFocus.h
    ui/RefreshOnFocus.cpp
)
target_include_directories(bld_sync_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(bld_sync_ui PUBLIC bld_sync bld_ui_common Qt6::Widgets)
