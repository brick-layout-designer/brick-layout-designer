# The design tokens, the synced settings and the "?" help buttons: what
# the sync screens (bld_sync_ui, which bld_ui links) share with the rest.
add_library(bld_ui_common STATIC
    theme/Tokens.cpp
    theme/Tokens.h
    theme/AppPrefs.cpp
    theme/AppPrefs.h
    help/HelpTexts.cpp
    help/HelpTexts.h
    help/HelpPages.cpp
    help/HelpPages.h
    help/HelpButton.cpp
    help/HelpButton.h
    help/SourceLinks.cpp
    help/SourceLinks.h
    ConfirmDialog.cpp
    ConfirmDialog.h
)
target_include_directories(bld_ui_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_ui_common PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
# Help pages fall back to the source tree's help/ in development builds.
target_compile_definitions(bld_ui_common PRIVATE BLD_HELP_SOURCE_DIR="${CMAKE_SOURCE_DIR}/help")

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
    MainWindowViews.cpp
    MapView.cpp
    MapView.h
    MapViewDrag.cpp
    MapViewClipboard.cpp
    MapViewContextMenu.cpp
    MapViewPaint.cpp
    MapViewTouch.cpp
    TouchActionBar.cpp
    TouchActionBar.h
    TouchMode.cpp
    TouchMode.h
    MapViewLoading.cpp
    MapViewInternal.h
    ConnectionSnap.cpp
    ConnectionSnap.h
    SnapFeel.cpp
    SnapFeel.h
    SelectionOverlay.cpp
    SelectionOverlay.h
    LayerPanel.cpp
    LayerPanel.h
    PartsBrowser.cpp
    PartsBrowser.h
    ModulesPanel.cpp
    ModulesPanel.h
    ModuleLookDialog.cpp
    SheetChoiceDialog.cpp
    SheetChoiceDialog.h
    ModuleLookDialog.h
    ModuleEditBar.cpp
    ModuleEditBar.h
    MapViewModules.cpp
    ModuleLibraryPanel.cpp
    ModuleThumbnail.cpp
    ModuleLibraryPanel.h
    PartDifferencesDialog.cpp
    PartDifferencesDialog.h
    PartUsagePanel.cpp
    PartUsagePanel.h
    SavedViews.cpp
    SavedViews.h
    SharePictureDialog.cpp
    SharePictureDialog.h
    ViewsPanel.cpp
    ViewsPanel.h
    LoadingCard.cpp
    NoticeArea.cpp
    NoticeArea.h
    LoadingCard.h
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
    venue/VenueDesignerDialog.cpp
    venue/VenueDesignerDialog.h
    venue/VenueDesignerView.cpp
    venue/VenueDesignerView.h
    MainWindowShell.cpp
    SettingsDialog.cpp
    SettingsDialog.h
    theme/ThemeManager.cpp
    theme/ThemeManager.h
    theme/Icons.cpp
    theme/Icons.h
    theme/PanelHeader.cpp
    theme/PanelHeader.h
    theme/theme.qrc
    tours/Tours.cpp
    tours/TourStrings.cpp
    tours/Tours.h
    tours/tours.qrc
    help/ShortcutsDialog.cpp
    help/ShortcutsDialog.h
)

target_link_libraries(bld_ui
    PUBLIC
        bld_ui_common
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

# Live layouts on a collaborative server (File › Connect to Server…).
target_sources(bld_ui PRIVATE LiveLayout.cpp LiveLayout.h MainWindowLive.cpp PrefsSync.cpp PrefsSync.h
    MainWindowModules.cpp ServerLibrary.cpp ServerLibrary.h SaveModuleDialog.cpp SaveModuleDialog.h)
target_link_libraries(bld_ui PUBLIC bld_sync_ui)
