add_library(bld_ui STATIC
    MainWindow.cpp
    MainWindow.h
    MainWindowBudgetMenu.cpp
    MainWindowFileIO.cpp
    MainWindowImport.cpp
    MainWindowLibrary.cpp
    MainWindowMapMenu.cpp
    MainWindowMenus.cpp
    MainWindowToolsMenu.cpp
    MapView.cpp
    MapView.h
    MapViewDrag.cpp
    MapViewClipboard.cpp
    MapViewContextMenu.cpp
    MapViewPaint.cpp
    MapViewInternal.h
    ConnectionSnap.cpp
    ConnectionSnap.h
    SelectionOverlay.cpp
    SelectionOverlay.h
    LayerPanel.cpp
    LayerPanel.h
    PartsBrowser.cpp
    PartsBrowser.h
    ModulesPanel.cpp
    ModulesPanel.h
    ModuleLibraryPanel.cpp
    ModuleLibraryPanel.h
    PartUsagePanel.cpp
    PartUsagePanel.h
    LibraryPathsDialog.cpp
    LibraryPathsDialog.h
    BackgroundTask.h
    DownloadCenterDialog.cpp
    DownloadCenterDialog.h
    EditDialogs.cpp
    EditDialogs.h
    ImportPipeline.cpp
    ImportPipeline.h
    ImportPreviewDialog.cpp
    ImportPreviewDialog.h
    PreferencesDialog.cpp
    PreferencesDialog.h
    FindDialog.cpp
    FindDialog.h
    BudgetDialog.cpp
    BudgetDialog.h
    BudgetSession.cpp
    UpdateCheck.cpp
    UpdateCheck.h
    BudgetSession.h
    VenueDialog.cpp
    VenueDialog.h
    VenueDimensionsDialog.cpp
    VenueDimensionsDialog.h
    VenueLibraryPanel.cpp
    VenueLibraryPanel.h
)

target_link_libraries(bld_ui
    PUBLIC
        bld_core
        bld_saveload
        bld_parts
        bld_rendering
        bld_edit
        bld_import
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        Qt6::PrintSupport
)


# Help > Contents falls back to the source tree's help/ in development builds.
target_compile_definitions(bld_ui PRIVATE BLD_HELP_SOURCE_DIR="${CMAKE_SOURCE_DIR}/help")

# Live layouts on a collaborative server (File › Connect to Server…), only
# with -DBLD_SYNC=ON.
if(BLD_SYNC)
    target_sources(bld_ui PRIVATE LiveLayout.cpp LiveLayout.h MainWindowLive.cpp)
    target_link_libraries(bld_ui PUBLIC bld_sync_ui)
    target_compile_definitions(bld_ui PUBLIC BLD_SYNC=1)
endif()
