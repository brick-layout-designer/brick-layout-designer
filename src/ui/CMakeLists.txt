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

# Download Center re-uses Qt's private QZipReader (same path as bld_import)
# to extract downloaded parts archives into a user-chosen library folder.
# Mirror the find_path search and BLD_NO_QZIPREADER fallback used there.
find_path(BLD_UI_QZIPREADER_INCLUDE_DIR
    NAMES private/qzipreader_p.h
    PATHS
        /usr/include/qt6/QtCore/${Qt6Core_VERSION}/QtCore
        /usr/include/qt6/QtCore
    NO_DEFAULT_PATH)
if(NOT BLD_UI_QZIPREADER_INCLUDE_DIR)
    get_target_property(_qt_core_includes Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_inc IN LISTS _qt_core_includes)
        file(GLOB _candidates LIST_DIRECTORIES true "${_inc}/*/QtCore")
        foreach(_c IN LISTS _candidates)
            if(EXISTS "${_c}/private/qzipreader_p.h")
                set(BLD_UI_QZIPREADER_INCLUDE_DIR "${_c}" CACHE INTERNAL "")
                break()
            endif()
        endforeach()
        if(BLD_UI_QZIPREADER_INCLUDE_DIR)
            break()
        endif()
    endforeach()
endif()
if(BLD_UI_QZIPREADER_INCLUDE_DIR)
    target_include_directories(bld_ui PRIVATE "${BLD_UI_QZIPREADER_INCLUDE_DIR}")
    get_filename_component(_qtcore_version_parent
        "${BLD_UI_QZIPREADER_INCLUDE_DIR}" DIRECTORY)
    target_include_directories(bld_ui PRIVATE "${_qtcore_version_parent}")
else()
    target_compile_definitions(bld_ui PRIVATE BLD_NO_QZIPREADER=1)
endif()
