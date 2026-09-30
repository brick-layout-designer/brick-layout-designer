# Live sync with the collaborative web server (docs: DESKTOP-LIVE-SYNC in the
# web repo). Phase P2 spike: yrs (Rust Yjs) linked through its C API, yffi,
# plus Qt WebSockets, on every platform CI builds. Only built with
# -DBLD_SYNC=ON; needs a current stable Rust toolchain (cargo on PATH).

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
corrosion_import_crate(
    MANIFEST_PATH "${yffi_SOURCE_DIR}/Cargo.toml"
    CRATE_TYPES staticlib
    LOCKED
    PROFILE release
)

find_package(Qt6 6.7 REQUIRED COMPONENTS WebSockets Widgets)

# QtKeychain keeps the server sign-in token in the OS keychain (macOS
# Keychain, Windows Credential Store, Secret Service on Linux). Built
# static from the pinned release; on Linux it needs libsecret-1.
set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
set(_bld_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(
    qtkeychain
    URL      https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/0.15.0.tar.gz
    URL_HASH SHA256=f4254dc8f0933b06d90672d683eab08ef770acd8336e44dfa030ce041dc2ca22
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(qtkeychain)
set(BUILD_SHARED_LIBS ${_bld_shared})

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
    SyncSession.h
    SyncSession.cpp
    Awareness.h
    Awareness.cpp
    Presence.h
    Presence.cpp
    PartsUpload.h
    PartsUpload.cpp
    TokenStore.h
    TokenStore.cpp
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
    ui/UploadPartsDialog.h
    ui/UploadPartsDialog.cpp
)
target_include_directories(bld_sync_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(bld_sync_ui PUBLIC bld_sync Qt6::Widgets)
