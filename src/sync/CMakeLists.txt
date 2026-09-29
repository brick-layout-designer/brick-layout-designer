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

find_package(Qt6 6.7 REQUIRED COMPONENTS WebSockets)

add_library(bld_sync STATIC
    YDocSummary.h
    YDocSummary.cpp
    DocJson.h
    DocJson.cpp
    WebModel.h
    WebModel.cpp
)
target_include_directories(bld_sync
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/yrs)
# `yrs` is yffi's library target (its [lib] name).
target_link_libraries(bld_sync PUBLIC bld_core Qt6::Core Qt6::Gui Qt6::WebSockets PRIVATE yrs)
