cmake_minimum_required(VERSION 3.24)

project(Trailer
    VERSION 0.1.0
    DESCRIPTION "Cross-platform PDF and image workbench"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(CompilerWarnings)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Concurrent Gui Widgets Network Svg Test Pdf PdfWidgets PrintSupport)
include(OnnxRuntime)
# Prefer a CMake package config (e.g. vcpkg on Windows).  Fall back to
# pkg-config so that Linux/macOS system packages (libqpdf-dev, brew qpdf)
# are found without requiring a custom CMAKE_PREFIX_PATH.
find_package(qpdf CONFIG QUIET)
if(NOT qpdf_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(QPDF REQUIRED IMPORTED_TARGET libqpdf)
  # Expose the same target name used in target_link_libraries below.
  if(NOT TARGET qpdf::libqpdf)
    add_library(qpdf::libqpdf ALIAS PkgConfig::QPDF)
  endif()
endif()

qt_standard_project_setup()

include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

set(TRAILER_SOURCES
    src/annotation/Annotation.h
    src/annotation/AnnotationStore.cpp
    src/annotation/AnnotationStore.h
    src/app/Application.cpp
    src/app/Application.h
    src/app/CommandLine.cpp
    src/app/CommandLine.h
    src/cards/CardStore.cpp
    src/cards/CardStore.h
    src/cards/MyCard.cpp
    src/cards/MyCard.h
    src/document/DocumentRegistry.cpp
    src/document/DocumentRegistry.h
    src/document/IDocument.h
    src/document/IFormatAdapter.h
    src/document/ImageAdapter.cpp
    src/document/ImageAdapter.h
    src/document/PdfAdapter.cpp
    src/document/PdfAdapter.h
    src/document/PdfCommands.cpp
    src/document/PdfCommands.h
    src/document/PdfEditor.cpp
    src/document/PdfEditor.h
    src/document/StubAdapter.cpp
    src/document/StubAdapter.h
    src/filters/ImageFilter.cpp
    src/filters/ImageFilter.h
    src/ml/BackgroundRemover.cpp
    src/ml/BackgroundRemover.h
    src/ml/ModelDownloader.cpp
    src/ml/ModelDownloader.h
    src/ml/ModelRegistry.cpp
    src/ml/ModelRegistry.h
    src/ml/OcrEngine.cpp
    src/ml/OcrEngine.h
    src/ml/OnnxSession.cpp
    src/ml/OnnxSession.h
    src/ml/SamSession.cpp
    src/ml/SamSession.h
    src/recent/RecentFiles.cpp
    src/recent/RecentFiles.h
    src/settings/AppPaths.cpp
    src/settings/AppPaths.h
    src/settings/Settings.cpp
    src/settings/Settings.h
    src/signatures/Signature.h
    src/signatures/SignatureStore.cpp
    src/signatures/SignatureStore.h
    src/ui/DocumentView.cpp
    src/ui/DocumentView.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/AnimationBar.cpp
    src/ui/AnimationBar.h
    src/ui/AnnotationOverlay.cpp
    src/ui/AnnotationOverlay.h
    src/ui/FormOverlay.cpp
    src/ui/FormOverlay.h
    src/ui/FormToolbar.cpp
    src/ui/FormToolbar.h
    src/ui/IconHelper.cpp
    src/ui/IconHelper.h
    src/ui/Inspector.cpp
    src/ui/Inspector.h
    src/ui/Magnifier.cpp
    src/ui/Magnifier.h
    src/ui/MarkupToolbar.cpp
    src/ui/MarkupToolbar.h
    src/ui/MyCardDialog.cpp
    src/ui/MyCardDialog.h
    src/ui/OcrResultsDialog.cpp
    src/ui/OcrResultsDialog.h
    src/ui/SamSegmentDialog.cpp
    src/ui/SamSegmentDialog.h
    src/ui/SearchBar.cpp
    src/ui/SearchBar.h
    src/ui/Sidebar.cpp
    src/ui/Sidebar.h
    src/ui/SignatureCaptureDialog.cpp
    src/ui/SignatureCaptureDialog.h
    src/ui/SignaturesDialog.cpp
    src/ui/SignaturesDialog.h
    src/ui/ThumbnailModel.cpp
    src/ui/ThumbnailModel.h
    src/platform/Share.h
)

# Per-platform Share-sheet source. macOS uses Objective-C++ to drive
# NSSharingServicePicker; everything else gets the stub that reports
# isAvailable() == false so the menu item can hide cleanly.
if(APPLE)
    list(APPEND TRAILER_SOURCES src/platform/Share.mm)
    set_source_files_properties(src/platform/Share.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TRAILER_SOURCES src/platform/Share_stub.cpp)
endif()

add_library(trailer_core STATIC ${TRAILER_SOURCES} resources/trailer.qrc)
target_include_directories(trailer_core PUBLIC src)
target_link_libraries(trailer_core
    PUBLIC
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Network
        Qt6::Svg
        Qt6::Widgets
        Qt6::Pdf
        Qt6::PdfWidgets
        Qt6::PrintSupport
        qpdf::libqpdf
        tomlplusplus::tomlplusplus
        onnxruntime::onnxruntime
)
if(APPLE)
    target_link_libraries(trailer_core PUBLIC "-framework AppKit")
endif()
trailer_set_warnings(trailer_core)

set(TRAILER_ICON_DIR "${CMAKE_SOURCE_DIR}/resources/icons")
set(TRAILER_MACOS_ICON "${TRAILER_ICON_DIR}/trailer.icns")
set(TRAILER_WINDOWS_ICON "${TRAILER_ICON_DIR}/trailer.ico")

# Generate a Windows resource (.rc) referencing the bundled .ico so Explorer,
# the taskbar, and Alt-Tab show the loupe icon. Done as a configure-time
# write so the .rc lives next to the binary's source list.
if(WIN32 AND EXISTS "${TRAILER_WINDOWS_ICON}")
    set(TRAILER_RC "${CMAKE_BINARY_DIR}/trailer.rc")
    file(WRITE "${TRAILER_RC}"
        "IDI_ICON1 ICON DISCARDABLE \"${TRAILER_WINDOWS_ICON}\"\n")
    set(TRAILER_WINDOWS_RC_SOURCE "${TRAILER_RC}")
else()
    set(TRAILER_WINDOWS_RC_SOURCE "")
endif()

qt_add_executable(trailer
    src/main.cpp
    ${TRAILER_WINDOWS_RC_SOURCE}
)
target_link_libraries(trailer PRIVATE trailer_core)
trailer_set_warnings(trailer)
trailer_deploy_onnxruntime(trailer)

# Copy the .icns into the macOS bundle's Resources/ directory so Finder/Dock
# pick it up, and tell Info.plist about the file via MACOSX_BUNDLE_ICON_FILE.
if(APPLE AND EXISTS "${TRAILER_MACOS_ICON}")
    target_sources(trailer PRIVATE "${TRAILER_MACOS_ICON}")
    set_source_files_properties("${TRAILER_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
endif()

set_target_properties(trailer PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Trailer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.trailer.Trailer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "trailer.icns"
    # Custom Info.plist that declares CFBundleDocumentTypes — without
    # this macOS doesn't dispatch QFileOpenEvent when the user drags
    # a file onto the Dock icon or right-clicks → Open With. The
    # default Qt-generated plist has no document-types entry.
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in"
)

# Installation rules (used by Linux packaging)
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS trailer DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES platform/linux/trailer.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES platform/linux/org.trailer.Trailer.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(_size 16 32 64 128 256 512)
        install(FILES resources/icons/trailer_${_size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
                RENAME trailer.png)
    endforeach()
endif()

# Developer-facing tool: renders every toolbar SVG into a sheet PNG
# for visual review. Not part of any test target so ctest doesn't run
# it, but it builds with the rest so it doesn't bit-rot.
qt_add_executable(generate_icon_sheet tools/generate_icon_sheet.cpp)
target_link_libraries(generate_icon_sheet PRIVATE trailer_core)
trailer_set_warnings(generate_icon_sheet)

enable_testing()
add_subdirectory(tests)
