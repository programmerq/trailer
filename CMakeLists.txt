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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Test)

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
    src/app/Application.cpp
    src/app/Application.h
    src/app/CommandLine.cpp
    src/app/CommandLine.h
    src/document/DocumentRegistry.cpp
    src/document/DocumentRegistry.h
    src/document/IDocument.h
    src/document/IFormatAdapter.h
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
    src/ui/Sidebar.cpp
    src/ui/Sidebar.h
)

add_library(trailer_core STATIC ${TRAILER_SOURCES})
target_include_directories(trailer_core PUBLIC src)
target_link_libraries(trailer_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
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

enable_testing()
add_subdirectory(tests)
