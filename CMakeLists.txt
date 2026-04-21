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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Test Pdf PdfWidgets PrintSupport)
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
    src/document/DocumentRegistry.cpp
    src/document/DocumentRegistry.h
    src/document/IDocument.h
    src/document/IFormatAdapter.h
    src/document/ImageAdapter.cpp
    src/document/ImageAdapter.h
    src/document/PdfAdapter.cpp
    src/document/PdfAdapter.h
    src/document/PdfEditor.cpp
    src/document/PdfEditor.h
    src/document/StubAdapter.cpp
    src/document/StubAdapter.h
    src/recent/RecentFiles.cpp
    src/recent/RecentFiles.h
    src/settings/AppPaths.cpp
    src/settings/AppPaths.h
    src/settings/Settings.cpp
    src/settings/Settings.h
    src/ui/DocumentView.cpp
    src/ui/DocumentView.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/AnimationBar.cpp
    src/ui/AnimationBar.h
    src/ui/AnnotationOverlay.cpp
    src/ui/AnnotationOverlay.h
    src/ui/Inspector.cpp
    src/ui/Inspector.h
    src/ui/Magnifier.cpp
    src/ui/Magnifier.h
    src/ui/MarkupToolbar.cpp
    src/ui/MarkupToolbar.h
    src/ui/SearchBar.cpp
    src/ui/SearchBar.h
    src/ui/Sidebar.cpp
    src/ui/Sidebar.h
    src/ui/ThumbnailModel.cpp
    src/ui/ThumbnailModel.h
)

add_library(trailer_core STATIC ${TRAILER_SOURCES})
target_include_directories(trailer_core PUBLIC src)
target_link_libraries(trailer_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Pdf
        Qt6::PdfWidgets
        Qt6::PrintSupport
        qpdf::libqpdf
        tomlplusplus::tomlplusplus
)
trailer_set_warnings(trailer_core)

qt_add_executable(trailer
    src/main.cpp
    resources/trailer.qrc
)
target_link_libraries(trailer PRIVATE trailer_core)
trailer_set_warnings(trailer)

set_target_properties(trailer PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Trailer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.trailer.Trailer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Linux-to-Windows mingw-w64 cross-compile workaround.
# Arch's mingw-w64-gcc defaults to MSVCRT, and recent MSVCRT import
# libs no longer export `__argc` as a data symbol, which Qt 6.10's
# prebuilt libQt6EntryPoint.a references unconditionally. Rather
# than patch the toolchain, skip Qt's WinMain wrapper and let
# main() live in the console subsystem. QApplication reads the
# command line via GetCommandLineW() internally, so this doesn't
# change runtime behaviour — just adds a console window when the
# cross-compiled .exe launches. Native Windows builds (vcpkg /
# aqtinstall via the Windows CI matrix) are unaffected.
if(MINGW AND CMAKE_CROSSCOMPILING)
    set_target_properties(trailer PROPERTIES
        WIN32_EXECUTABLE OFF
        QT_NO_ENTRYPOINT ON
    )
endif()

enable_testing()
add_subdirectory(tests)
