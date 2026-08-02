cmake_minimum_required(VERSION 3.24)

# Single source of truth for the human-visible version. VERSION holds
# only the BASE identity: a bare "X.Y.Z-dev" during development, a clean
# "X.Y.Z" for a release, or "X.Y.Z-rcN" for a release candidate. The CI
# release-candidate gate rejects the -dev / -rc suffixes before a release
# tag is cut.
#
# Two derived values come out of this block:
#   TRAILER_VERSION_NUMERIC  dotted MAJOR.MINOR.PATCH only. Feeds
#       project(VERSION ...), CFBundleShortVersionString/CFBundleVersion,
#       and any other format-constrained (dotted-numeric) consumer. It
#       NEVER carries a suffix or +build-metadata.
#   TRAILER_VERSION_STRING   the full human-visible string exposed via
#       the generated TrailerVersion.h (About box / setApplicationVersion).
#       For a "-dev" base in a git checkout this is self-identifying:
#       "X.Y.Z-dev+<commit-count>.g<shortsha>[.dirty]", so every dev
#       artifact is unique and traceable back to a commit with no manual
#       bump. Releases, RCs, and .git-less source tarballs keep the BASE
#       string unchanged.
file(READ "${CMAKE_SOURCE_DIR}/VERSION" TRAILER_VERSION_BASE)
string(STRIP "${TRAILER_VERSION_BASE}" TRAILER_VERSION_BASE)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" TRAILER_VERSION_NUMERIC
       "${TRAILER_VERSION_BASE}")

# Default: the full string is the base, unchanged (release / RC / tarball).
set(TRAILER_VERSION_STRING "${TRAILER_VERSION_BASE}")

# TRAILER_BUILD_COMMIT_COUNT: the git commit count on the checked-out
# HEAD, independent of the VERSION suffix. Nightly builds (nightly.yml)
# always build off main HEAD, and main's commit count only ever goes up,
# so this is the update channel's version-compare key (see
# src/update/UpdateManager.cpp and docs/decision-records/2026-07-30-
# nightly-auto-update-channel.md) — a plain integer compare, no date
# parsing, no ambiguity about two builds on the same calendar day.
# Defaults to 0 for a .git-less source tarball (never a valid "newer"
# build, so the update checker can never mistake it for one).
set(TRAILER_GIT_COMMIT_COUNT 0)
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _trailer_commit_count
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _trailer_commit_count_rc)
        if(_trailer_commit_count_rc EQUAL 0 AND _trailer_commit_count)
            set(TRAILER_GIT_COMMIT_COUNT "${_trailer_commit_count}")
        endif()
    endif()
endif()

# Only a bare "-dev" base opts into git-derived build metadata. A -dev.N
# counter, -rcN, or clean X.Y.Z is left alone.
if(TRAILER_VERSION_BASE MATCHES "-dev$" AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        # Every git call is guarded: a shallow/broken/absent repo must
        # fall back to the BASE string, never hard-error configuration.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_inside_wt
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _git_rc)
        if(_git_rc EQUAL 0 AND _git_inside_wt STREQUAL "true")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE _git_count
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _git_count_rc)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE _git_sha
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _git_sha_rc)
            if(_git_count_rc EQUAL 0 AND _git_sha_rc EQUAL 0
               AND _git_count AND _git_sha)
                # Dirty working tree → ".dirty" so a build off uncommitted
                # edits is never confused with the clean commit.
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" status --porcelain
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    OUTPUT_VARIABLE _git_status
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
                set(_git_dirty "")
                if(NOT _git_status STREQUAL "")
                    set(_git_dirty ".dirty")
                endif()
                set(TRAILER_VERSION_STRING
                    "${TRAILER_VERSION_BASE}+${_git_count}.g${_git_sha}${_git_dirty}")
            endif()
        endif()
    endif()
endif()

# Re-run configure when HEAD moves or the index changes so the derived
# metadata (commit count / sha / dirty) stays fresh across dev builds.
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/.git/HEAD")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/index")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/.git/index")
endif()

project(Trailer
    VERSION ${TRAILER_VERSION_NUMERIC}
    DESCRIPTION "Cross-platform PDF and image workbench"
    # C is enabled solely for the vendored TweetNaCl source
    # (third_party/tweetnacl/tweetnacl.c — see its NOTICE.md); every
    # other Trailer source is C++.
    LANGUAGES CXX C
)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/TrailerVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/TrailerVersion.h"
    @ONLY
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(TRAILER_WERROR "Treat compiler warnings as errors (enabled in CI)" OFF)
option(TRAILER_ADAPTIVE_ICON "Compile the adaptive light/dark macOS app icon via actool (requires full Xcode)" ON)

# Developer-only UX session recorder (see docs/ux-recorder.md). OFF by
# default: release artifacts must not link capture frameworks, request
# recording permissions, or carry recorder UI. When ON, the recorder
# still does nothing unless the app is launched with --ux-record.
# Everything it records stays on the local disk — no network code is
# permitted anywhere in src/uxrecord/ (PHILOSOPHY.md "No telemetry").
option(TRAILER_ENABLE_UX_RECORDER
       "Build the local developer UX session recorder (--ux-record)" OFF)

# Extra Info.plist keys injected only into recorder-enabled bundles.
# NSCameraUsageDescription must be present before AVFoundation may even
# *ask* for camera access (macOS kills the process otherwise). Default
# builds get an empty substitution so the shipped plist is unchanged.
set(TRAILER_UX_PLIST_EXTRAS "")
if(TRAILER_ENABLE_UX_RECORDER)
    string(CONCAT TRAILER_UX_PLIST_EXTRAS
        "<key>NSCameraUsageDescription</key>\n"
        "    <string>Trailer's developer UX recorder captures your camera "
        "locally during --ux-record sessions. Nothing is transmitted "
        "anywhere.</string>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(CompilerWarnings)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Concurrent Gui Widgets Network Svg Test Pdf PdfWidgets PrintSupport)
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
    src/annotation/AnnotationJson.cpp
    src/annotation/AnnotationJson.h
    src/annotation/AnnotationStore.cpp
    src/annotation/AnnotationStore.h
    src/app/Application.cpp
    src/app/Application.h
    src/app/CommandLine.cpp
    src/app/CommandLine.h
    src/diagnostics/FeedbackReport.cpp
    src/diagnostics/FeedbackReport.h
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
    src/document/CapabilityNotifier.h
    src/document/PageChangeNotifier.h
    src/document/ExternalChangeMonitor.cpp
    src/document/ExternalChangeMonitor.h
    src/document/ExternalChangeState.cpp
    src/document/ExternalChangeState.h
    src/document/PdfAdapter.cpp
    src/document/PdfAdapter.h
    src/document/PdfCommands.cpp
    src/document/PdfCommands.h
    src/document/RecoveryStore.cpp
    src/document/RecoveryStore.h
    src/document/ZoomStops.h
    src/document/OcrDiskCache.cpp
    src/document/OcrDiskCache.h
    src/document/PdfEditor.cpp
    src/document/PdfEditor.h
    src/document/SelectableTextStore.cpp
    src/document/SelectableTextStore.h
    src/document/SpreadLayout.cpp
    src/document/SpreadLayout.h
    src/document/StubAdapter.cpp
    src/document/StubAdapter.h
    src/filters/ImageFilter.cpp
    src/filters/ImageFilter.h
    src/ml/BackgroundCandidateScorer.cpp
    src/ml/BackgroundCandidateScorer.h
    src/ml/BackgroundRemover.cpp
    src/ml/BackgroundRemover.h
    src/ml/CancellationToken.h
    src/ml/MlScheduler.cpp
    src/ml/MlScheduler.h
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
    src/platform/PowerSource.cpp
    src/platform/PowerSource.h
    src/platform/ScreenCapturePermission.cpp
    src/platform/ScreenCapturePermission.h
    src/recent/RecentFiles.cpp
    src/recent/RecentFiles.h
    src/settings/AppPaths.cpp
    src/settings/AppPaths.h
    src/settings/DocumentTypeDefaults.cpp
    src/settings/DocumentTypeDefaults.h
    src/settings/SessionDraftStore.cpp
    src/settings/SessionDraftStore.h
    src/settings/Settings.cpp
    src/settings/Settings.h
    src/signatures/Signature.h
    src/signatures/SignatureStore.cpp
    src/signatures/SignatureStore.h
    src/ui/DocumentView.cpp
    src/ui/DocumentView.h
    src/ui/EmptyStateWidget.cpp
    src/ui/EmptyStateWidget.h
    src/ui/FeedbackDialog.cpp
    src/ui/FeedbackDialog.h
    src/ui/FileChangeBanner.cpp
    src/ui/FileChangeBanner.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MlProgressWidget.cpp
    src/ui/MlProgressWidget.h
    src/ui/ModelManagerDialog.cpp
    src/ui/ModelManagerDialog.h
    src/ui/OcrController.cpp
    src/ui/OcrController.h
    src/ui/AnimationBar.cpp
    src/ui/AnimationBar.h
    src/ui/AnnotationOverlay.cpp
    src/ui/AnnotationOverlay.h
    src/ui/CropPagesDialog.cpp
    src/ui/CropPagesDialog.h
    src/ui/PasswordExportDialog.cpp
    src/ui/PasswordExportDialog.h
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
    src/ui/PreferencesDialog.cpp
    src/ui/PreferencesDialog.h
    src/ui/SamController.cpp
    src/ui/SamController.h
    src/ui/SearchBar.cpp
    src/ui/SearchBar.h
    src/ui/SelectableTextLayer.cpp
    src/ui/SelectableTextLayer.h
    src/ui/Sidebar.cpp
    src/ui/Sidebar.h
    src/ui/TwoPageView.cpp
    src/ui/TwoPageView.h
    src/ui/SignatureCaptureDialog.cpp
    src/ui/SignatureCaptureDialog.h
    src/ui/SignaturePicker.cpp
    src/ui/SignaturePicker.h
    src/ui/SignaturesDialog.cpp
    src/ui/SignaturesDialog.h
    src/ui/ThumbnailModel.cpp
    src/ui/ThumbnailModel.h
    src/update/Ed25519.cpp
    src/update/Ed25519.h
    src/update/UpdateChecker.cpp
    src/update/UpdateChecker.h
    src/update/UpdateFeedParser.cpp
    src/update/UpdateFeedParser.h
    src/update/UpdateManager.cpp
    src/update/UpdateManager.h
    src/update/UpdatePublicKey.h
    src/update/UpdateTypes.cpp
    src/update/UpdateTypes.h
    src/util/CaptureScale.cpp
    src/util/CaptureScale.h
    src/util/TempPath.h
    src/platform/Share.h
    src/platform/QuitMenu.h
    src/platform/DockRecents.h
    src/uxrecord/UxRecord.h
)

# Pure, platform-agnostic capture-backend policy (string<->enum mapping and
# the backend-selection rule) builds everywhere; only the native probe and
# the SCContentSharingPicker capture are per-platform, split below.
list(APPEND TRAILER_SOURCES src/platform/ScreenCaptureBackend.cpp)

# XDG screenshot-portal capture backend (Wayland). The real QtDBus
# implementation is Linux/BSD-only; macOS and Windows link the stub so the
# shared non-macOS capture path resolves the portal as unavailable and stays
# on QScreen::grabWindow. Qt6::DBus is linked only where the real one builds.
if(UNIX AND NOT APPLE)
    list(APPEND TRAILER_SOURCES src/platform/PortalScreenshot.cpp)
    # QtDBus is part of qtbase but not in the default component list; the
    # portal backend is the only consumer, so pull it in only here.
    find_package(Qt6 6.6 REQUIRED COMPONENTS DBus)
    set(TRAILER_NEEDS_QTDBUS ON)
else()
    list(APPEND TRAILER_SOURCES src/platform/PortalScreenshot_stub.cpp)
endif()
list(APPEND TRAILER_SOURCES src/platform/PortalScreenshot.h)

# UX recorder sources are only compiled into recorder-enabled builds.
# src/uxrecord/UxRecord.h (above) is the always-present facade whose
# functions inline to no-ops when TRAILER_UX_RECORDER is undefined, so
# call sites in MainWindow/Application need no #ifdef of their own.
if(TRAILER_ENABLE_UX_RECORDER)
    list(APPEND TRAILER_SOURCES
        src/uxrecord/UxEventStream.cpp
        src/uxrecord/UxEventStream.h
        src/uxrecord/UxPlatformCapture.h
        src/uxrecord/UxQtEventCapture.cpp
        src/uxrecord/UxQtEventCapture.h
        src/uxrecord/UxRecord.cpp
        src/uxrecord/UxRecorder.cpp
        src/uxrecord/UxRecorder.h
        src/uxrecord/UxTrailerHooks.cpp
    )
    if(APPLE)
        list(APPEND TRAILER_SOURCES src/uxrecord/MacUxPlatformCapture.mm)
        set_source_files_properties(src/uxrecord/MacUxPlatformCapture.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    else()
        # Non-macOS recorder builds compile against a stub capture
        # backend: the Qt-level event stream and session report still
        # work; screen/camera/global-input capture report themselves
        # as unavailable in events.jsonl.
        list(APPEND TRAILER_SOURCES src/uxrecord/StubUxPlatformCapture.cpp)
    endif()
endif()

# Per-platform Share-sheet source. macOS uses Objective-C++ to drive
# NSSharingServicePicker; everything else gets the stub that reports
# isAvailable() == false so the menu item can hide cleanly.
if(APPLE)
    list(APPEND TRAILER_SOURCES src/platform/Share.mm)
    set_source_files_properties(src/platform/Share.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
    # ScreenCaptureKit picker backend (Apple-only Objective-C++). NOT built
    # in CI — see the header comment in MacScreenCapture.mm; requires an
    # on-device build + the GPSC.2 smoke test before its flag default flips.
    list(APPEND TRAILER_SOURCES src/platform/MacScreenCapture.mm)
    set_source_files_properties(src/platform/MacScreenCapture.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TRAILER_SOURCES src/platform/Share_stub.cpp)
    # Non-Apple stub: same symbols, screenCaptureKitAvailable() == false so
    # the picker backend is never selected off-Mac.
    list(APPEND TRAILER_SOURCES src/platform/MacScreenCapture_stub.cpp)
endif()

# Per-platform "Quit and Keep Windows" native menu shim. macOS uses
# Objective-C++ to install the NSMenuItem.alternate in-place Option swap
# and to read NSQuitAlwaysKeepsWindows; everything else links the no-op
# stub (the functional QAction + requestQuit + draft store work without it).
if(APPLE)
    list(APPEND TRAILER_SOURCES src/platform/QuitMenu.mm)
    set_source_files_properties(src/platform/QuitMenu.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TRAILER_SOURCES src/platform/QuitMenu_stub.cpp)
endif()

# Per-platform "Reduce Motion" accessibility query (docs/accessibility-
# checklist.md row A6). macOS reads the real NSWorkspace toggle;
# everything else falls back to a documented best-effort Qt UI-effect
# proxy (see ReducedMotion_stub.cpp). ReducedMotion.cpp is the
# cross-platform override wrapper used by every caller and is always
# built, mirroring the Share.mm / Share_stub.cpp pattern.
if(APPLE)
    list(APPEND TRAILER_SOURCES src/platform/ReducedMotion.mm)
    set_source_files_properties(src/platform/ReducedMotion.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TRAILER_SOURCES src/platform/ReducedMotion_stub.cpp)
endif()
list(APPEND TRAILER_SOURCES src/platform/ReducedMotion.cpp src/platform/ReducedMotion.h)

# Per-platform "what scale does the clipboard declare for its image?" query,
# used to open a pasted HiDPI screen capture at its true size (see
# src/util/CaptureScale.h). macOS reads NSPasteboard's bitmap representation;
# everything else links the stub that honestly reports "nothing declared".
if(APPLE)
    list(APPEND TRAILER_SOURCES src/platform/ClipboardScale.mm)
    set_source_files_properties(src/platform/ClipboardScale.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TRAILER_SOURCES src/platform/ClipboardScale_stub.cpp)
endif()
list(APPEND TRAILER_SOURCES src/platform/ClipboardScale.h)

# Per-platform Dock-icon "recents while not running" shim. macOS uses
# Objective-C++ to register opened documents with NSDocumentController so
# Launch Services can render them in the Dock icon's right-click menu even
# with Trailer's process dead; everything else links the no-op stub (the
# in-app File > Open Recent menu works unchanged without it — see
# src/platform/DockRecents.h for the full mechanism writeup).
if(APPLE)
    list(APPEND TRAILER_SOURCES src/platform/DockRecents.mm)
    set_source_files_properties(src/platform/DockRecents.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TRAILER_SOURCES src/platform/DockRecents_stub.cpp)
endif()

# TweetNaCl (public domain, vendored verbatim — see
# third_party/tweetnacl/NOTICE.md) provides the ed25519 signature
# verification behind the nightly update channel (src/update/Ed25519.*).
# Built as its own tiny static lib, in C, with warnings suppressed: it's
# unmodified upstream source, so Trailer's own -Wall/-Werror posture
# (trailer_set_warnings) doesn't apply to it and would just generate
# noise to ignore forever.
add_library(tweetnacl STATIC third_party/tweetnacl/tweetnacl.c)
target_include_directories(tweetnacl PUBLIC third_party/tweetnacl)
# Plain C, no Qt macros — qt_standard_project_setup() turns AUTOMOC/
# AUTOUIC/AUTORCC on project-wide, which is harmless-but-pointless here;
# switch them off explicitly for clarity.
set_target_properties(tweetnacl PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(MSVC)
    target_compile_options(tweetnacl PRIVATE /w)
else()
    target_compile_options(tweetnacl PRIVATE -w)
endif()

add_library(trailer_core STATIC ${TRAILER_SOURCES} resources/trailer.qrc)
target_include_directories(trailer_core
    PUBLIC
        src
        "${CMAKE_BINARY_DIR}/generated"
)
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
        tweetnacl
)
if(TRAILER_NEEDS_QTDBUS)
    # Linux/BSD only: the XDG screenshot-portal backend (PortalScreenshot.cpp)
    # talks to org.freedesktop.portal.Desktop over the session bus.
    target_link_libraries(trailer_core PUBLIC Qt6::DBus)
endif()

if(APPLE)
    target_link_libraries(trailer_core PUBLIC "-framework AppKit")
    # PowerSource.cpp uses IOPSGetProvidingPowerSourceType from
    # IOKit/ps/IOPowerSources.h. IOKit + CoreFoundation are macOS-
    # only frameworks; gated inside the APPLE block so the default
    # link line on Linux / Windows stays free of macOS-isms.
    target_link_libraries(trailer_core PRIVATE "-framework IOKit" "-framework CoreFoundation")
    # CoreGraphics serves two macOS capture paths:
    #   * ScreenCapturePermission.cpp (PR #77) calls CGPreflightScreenCaptureAccess
    #     and CGRequestScreenCaptureAccess to preflight/request the Screen
    #     Recording TCC state (the screen-capture preflight ADR).
    #   * ScreenCaptureKit picker backend (MacScreenCapture.mm, PR #72): the
    #     picker + SCScreenshotManager live in ScreenCaptureKit; CoreMedia /
    #     CoreGraphics carry the sample/image types, and ImageIO +
    #     UniformTypeIdentifiers do the CGImage->PNG encode.
    # CoreGraphics is listed once here (de-duped on the #77/#72 merge) so it
    # isn't linked twice. All macOS-only; behind if(APPLE) so the
    # Linux/Windows link line stays clean.
    target_link_libraries(trailer_core PRIVATE
        "-framework ScreenCaptureKit"
        "-framework CoreMedia"
        "-framework CoreGraphics"
        "-framework ImageIO"
        "-framework UniformTypeIdentifiers")
endif()
if(TRAILER_ENABLE_UX_RECORDER)
    # PUBLIC so the always-present facade header (src/uxrecord/UxRecord.h)
    # selects its real declarations in every consumer (app, tools, tests).
    target_compile_definitions(trailer_core PUBLIC TRAILER_UX_RECORDER)
    if(APPLE)
        # Capture frameworks are linked ONLY into recorder builds, so a
        # default build's binary carries no recording capability at all.
        target_link_libraries(trailer_core PRIVATE
            "-framework AVFoundation"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework ImageIO"
            "-framework CoreGraphics"
        )
        # ScreenCaptureKit needs macOS 12.3+ but Trailer's deployment
        # target is 11.0 (packaging-macos.md). Weak-link so the binary
        # still loads on older systems; MacUxPlatformCapture.mm guards
        # every use with @available and records "unsupported" events
        # instead of capturing frames there.
        target_link_options(trailer_core PUBLIC
            "-Wl,-weak_framework,ScreenCaptureKit")
    endif()
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

# Title-case the macOS .app + the executable inside it (Trailer.app /
# Contents/MacOS/Trailer). Matches Mac convention — every other app in
# /Applications is Title Case. On Linux + Windows we keep the lowercase
# `trailer` / `trailer.exe` shell-friendly name, since those platforms
# don't lean as hard on visible casing.
if(APPLE)
    set_target_properties(trailer PROPERTIES OUTPUT_NAME "Trailer")
endif()

# Copy the .icns into the macOS bundle's Resources/ directory so Finder/Dock
# pick it up, and tell Info.plist about the file via MACOSX_BUNDLE_ICON_FILE.
# This stays as the pre-catalog fallback (CFBundleIconFile) that coexists with
# the adaptive Assets.car / CFBundleIconName wiring below.
if(APPLE AND EXISTS "${TRAILER_MACOS_ICON}")
    target_sources(trailer PRIVATE "${TRAILER_MACOS_ICON}")
    set_source_files_properties("${TRAILER_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
endif()

# Adaptive (light/dark) app icon — ADR 0009 Option A. Compile the luminosity
# Asset Catalog (resources/macos/Assets.xcassets, AppIcon set with light +
# `luminosity: dark` variants fed by the existing trailer_*/trailer-dark_*
# PNGs) into Assets.car and drop it into the bundle's Resources/ via actool.
# Info.plist.in declares CFBundleIconName=AppIcon alongside the legacy
# CFBundleIconFile so the two coexist.
#
# EVERYTHING here is inside `if(APPLE)` and depends on `actool`, which ships
# only with Xcode — so it is completely inert on Linux/Windows: the Linux
# `linux-build` and `windows-cross` CI lanes never evaluate any of it, and the
# .xcassets files are plain data on those platforms. actool is invoked via
# `xcrun` (a POST_BUILD step on the trailer target) so it runs during
# `cmake --build`, before macdeployqt in scripts/build-macos.sh.
if(APPLE AND TRAILER_ADAPTIVE_ICON)
    set(TRAILER_XCASSETS "${CMAKE_SOURCE_DIR}/resources/macos/Assets.xcassets")
    if(EXISTS "${TRAILER_XCASSETS}")
        # Match the bundle's LSMinimumSystemVersion floor (Info.plist.in: 14.0)
        # rather than CMAKE_OSX_DEPLOYMENT_TARGET, which may be unset on a bare
        # `cmake` invocation on a Mac and would hand actool an empty argument.
        set(TRAILER_ICON_MIN_MACOS "14.0")
        # actool REQUIRES --output-partial-info-plist whenever the catalog
        # contains app icons: without it actool emits the notice "Compiling
        # app icons requires passing --output-partial-info-plist [path]" and
        # refuses to fully compile the AppIcon set, so no Assets.car is
        # written and scripts/build-macos.sh's verifier fails. We point it at
        # a throwaway file in the build dir purely to satisfy that contract —
        # its contents (CFBundleIconName=AppIcon) are NOT merged, because
        # resources/macos/Info.plist.in already hardcodes CFBundleIconName.
        set(TRAILER_ICON_PARTIAL_PLIST
            "${CMAKE_CURRENT_BINARY_DIR}/actool-AppIcon-partial.plist")
        add_custom_command(TARGET trailer POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "$<TARGET_BUNDLE_CONTENT_DIR:trailer>/Resources"
            COMMAND xcrun actool
                    --app-icon AppIcon
                    --output-partial-info-plist "${TRAILER_ICON_PARTIAL_PLIST}"
                    --minimum-deployment-target "${TRAILER_ICON_MIN_MACOS}"
                    --platform macosx
                    --target-device mac
                    --compile "$<TARGET_BUNDLE_CONTENT_DIR:trailer>/Resources"
                    "${TRAILER_XCASSETS}"
            # NB: no BYPRODUCTS here -- target-context genexes
            # ($<TARGET_BUNDLE_CONTENT_DIR:...>) are not valid in BYPRODUCTS/OUTPUT
            # (no head target) and break CMake generate. actool still writes
            # Assets.car via the COMMAND above.
            COMMENT "Compiling Assets.xcassets -> Assets.car (AppIcon light/dark) via actool"
            VERBATIM
        )
    endif()
elseif(APPLE)
    message(STATUS "TRAILER_ADAPTIVE_ICON=OFF — skipping actool app-icon compile; app will use the static (non-adaptive) .icns icon")
endif()

set_target_properties(trailer PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Trailer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.programmerq.trailer"
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
    # GNUInstallDirs derives DOCDIR from the (capitalized) project name, so its
    # default here is share/doc/Trailer. Force the lowercase package-name
    # spelling so a naive `cmake --install` (without the packaging scripts'
    # -DCMAKE_INSTALL_DOCDIR override) still lands the license texts where the
    # .deb/.rpm expect them. A normal variable set after include() shadows the
    # cache value for the install() rules below.
    set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/trailer")
    install(TARGETS trailer DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES platform/linux/trailer.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES platform/linux/io.github.programmerq.trailer.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(_size 16 32 64 128 256 512)
        install(FILES resources/icons/trailer_${_size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
                RENAME trailer.png)
    endforeach()

    # License / attribution files. Ship the first-party MIT license, the
    # third-party dependency summary, and the verbatim upstream license texts
    # so the packaged artifact satisfies each dependency's redistribution
    # terms. CMAKE_INSTALL_DOCDIR is pinned to share/doc/trailer just above.
    install(FILES LICENSE THIRD_PARTY_LICENSES.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY licenses/third-party
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
endif()

# Developer-facing tool: renders every toolbar SVG into a sheet PNG
# for visual review. Not part of any test target so ctest doesn't run
# it, but it builds with the rest so it doesn't bit-rot.
qt_add_executable(generate_icon_sheet tools/generate_icon_sheet.cpp)
target_link_libraries(generate_icon_sheet PRIVATE trailer_core)
trailer_set_warnings(generate_icon_sheet)

# grab_screenshots — drives Application + MainWindow under the
# offscreen plugin to write reference PNGs into docs/screenshots/.
# Used to keep the Windows docs (and PR bodies for UI-visible work)
# stocked without needing a real desktop session. Builds alongside
# everything else so it doesn't bit-rot when the MainWindow API
# evolves.
qt_add_executable(grab_screenshots tools/grab_screenshots.cpp)
target_link_libraries(grab_screenshots PRIVATE trailer_core)
trailer_set_warnings(grab_screenshots)

enable_testing()
add_subdirectory(tests)
