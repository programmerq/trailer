# Provides an `onnxruntime::onnxruntime` IMPORTED target, preferring an
# installed SDK (vcpkg on Windows, brew on macOS, a system package on
# Linux) and falling back to Microsoft's official pre-built tarballs
# from their GitHub releases page.
#
# The tarball path is the canonical one because it pins a specific
# version with SHA256 verification, which keeps builds reproducible
# across dev machines and CI. The `find_package` shortcut is only
# there so that developers who already have ORT installed don't pay
# the download twice.
#
# After inclusion, downstream targets should link against
# `onnxruntime::onnxruntime`.

set(TRAILER_ORT_VERSION "1.25.0")

# Allow the developer to short-circuit the detection/download logic by
# setting `-DTRAILER_ORT_ROOT=/path/to/extracted/tarball`.
if(TRAILER_ORT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${TRAILER_ORT_ROOT}")
endif()

# First preference: an already-installed ORT package.
find_package(onnxruntime CONFIG QUIET)

if(NOT onnxruntime_FOUND)
    # Per-platform asset matrix. Microsoft publishes exactly these names
    # on the releases page; they do NOT ship a macOS x64 tarball anymore
    # (the osx-arm64 tarball is arm64-only; macOS Intel users must build
    # from source or use a system package).
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(_trailer_ort_archive "onnxruntime-osx-arm64-${TRAILER_ORT_VERSION}.tgz")
            set(_trailer_ort_sha256 "65405dc8793c86cadb98b5e07f6d3bdde84f8300f1b030d4736b41c17610d6c1")
            set(_trailer_ort_library_rel "lib/libonnxruntime.dylib")
        else()
            message(FATAL_ERROR
                "ONNX Runtime tarball for macOS x86_64 is not published "
                "upstream. Install via `brew install onnxruntime` or set "
                "TRAILER_ORT_ROOT to a locally-built SDK.")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_trailer_ort_archive "onnxruntime-linux-aarch64-${TRAILER_ORT_VERSION}.tgz")
            set(_trailer_ort_sha256 "849c04634e76446bbe0a92f67955a9641415c37f11930804066057bf9eadbd03")
        else()
            set(_trailer_ort_archive "onnxruntime-linux-x64-${TRAILER_ORT_VERSION}.tgz")
            set(_trailer_ort_sha256 "e0a8998e70416801f9a634a8ea1d369a255ff109741469f9d99cf369a46a1492")
        endif()
        set(_trailer_ort_library_rel "lib/libonnxruntime.so")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_trailer_ort_archive "onnxruntime-win-x64-${TRAILER_ORT_VERSION}.zip")
        set(_trailer_ort_sha256 "da753f762bf2400e7191ec594086b186a7051d5af8dc886f6e2020c2403df738")
        set(_trailer_ort_library_rel "lib/onnxruntime.lib")
        set(_trailer_ort_runtime_rel "lib/onnxruntime.dll")
    else()
        message(FATAL_ERROR "Unsupported platform for ONNX Runtime: ${CMAKE_SYSTEM_NAME}")
    endif()

    set(_trailer_ort_url
        "https://github.com/microsoft/onnxruntime/releases/download/v${TRAILER_ORT_VERSION}/${_trailer_ort_archive}")

    include(FetchContent)
    FetchContent_Declare(
        onnxruntime_prebuilt
        URL      ${_trailer_ort_url}
        URL_HASH SHA256=${_trailer_ort_sha256}
    )
    FetchContent_MakeAvailable(onnxruntime_prebuilt)

    set(_trailer_ort_include_dir "${onnxruntime_prebuilt_SOURCE_DIR}/include")
    set(_trailer_ort_library "${onnxruntime_prebuilt_SOURCE_DIR}/${_trailer_ort_library_rel}")
    if(WIN32)
        set(_trailer_ort_runtime "${onnxruntime_prebuilt_SOURCE_DIR}/${_trailer_ort_runtime_rel}")
    endif()

    if(NOT EXISTS "${_trailer_ort_include_dir}/onnxruntime_cxx_api.h")
        message(FATAL_ERROR
            "ONNX Runtime headers not found in downloaded SDK at "
            "${_trailer_ort_include_dir}")
    endif()

    if(NOT EXISTS "${_trailer_ort_library}")
        message(FATAL_ERROR
            "ONNX Runtime library not found in downloaded SDK at "
            "${_trailer_ort_library}")
    endif()

    if(WIN32 AND NOT EXISTS "${_trailer_ort_runtime}")
        message(FATAL_ERROR
            "ONNX Runtime runtime DLL not found in downloaded SDK at "
            "${_trailer_ort_runtime}")
    endif()

    if(NOT TARGET onnxruntime::onnxruntime)
        add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
    endif()

    if(WIN32)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${_trailer_ort_library}"
            IMPORTED_IMPLIB_RELEASE "${_trailer_ort_library}"
            IMPORTED_LOCATION "${_trailer_ort_runtime}"
            IMPORTED_LOCATION_RELEASE "${_trailer_ort_runtime}"
            INTERFACE_INCLUDE_DIRECTORIES "${_trailer_ort_include_dir}")
    else()
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${_trailer_ort_library}"
            IMPORTED_LOCATION_RELEASE "${_trailer_ort_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_trailer_ort_include_dir}")
    endif()
endif()

if(NOT TARGET onnxruntime::onnxruntime)
    message(FATAL_ERROR "ONNX Runtime was located but did not export onnxruntime::onnxruntime")
endif()

# Workaround for a long-standing quirk in Microsoft's pre-built
# tarballs: the generated onnxruntimeTargets.cmake advertises
# INTERFACE_INCLUDE_DIRECTORIES pointing at `include/onnxruntime`, but
# the tarball actually puts headers directly under `include/`. Without
# this fix CMake fails the generate step because the advertised path
# doesn't exist.
#
# Detect the mismatch by checking whether onnxruntime_cxx_api.h lives
# at the advertised path; if not, strip the `/onnxruntime` suffix.
get_target_property(_trailer_ort_inc onnxruntime::onnxruntime INTERFACE_INCLUDE_DIRECTORIES)
if(_trailer_ort_inc)
    set(_trailer_ort_fixed "")
    foreach(_dir ${_trailer_ort_inc})
        if(NOT EXISTS "${_dir}/onnxruntime_cxx_api.h")
            # The advertised path is wrong. Try stripping the last
            # component via a normalised absolute path — EXISTS can't
            # see through a non-existent intermediate directory, so we
            # have to resolve the parent first then check.
            get_filename_component(_candidate "${_dir}" DIRECTORY)
            if(EXISTS "${_candidate}/onnxruntime_cxx_api.h")
                set(_dir "${_candidate}")
            endif()
        endif()
        list(APPEND _trailer_ort_fixed "${_dir}")
    endforeach()
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_trailer_ort_fixed}")
endif()

# Runtime library deployment: the `trailer` executable needs
# libonnxruntime next to it on Windows/macOS or in the RPATH on Linux.
# Downstream CMakeLists.txt can call `trailer_deploy_onnxruntime(<tgt>)`
# to set that up once the tarball path is known.
function(trailer_deploy_onnxruntime target)
    get_target_property(_ort_loc onnxruntime::onnxruntime IMPORTED_LOCATION_RELEASE)
    if(NOT _ort_loc)
        get_target_property(_ort_loc onnxruntime::onnxruntime IMPORTED_LOCATION)
    endif()
    if(_ort_loc AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_ort_loc}" "$<TARGET_FILE_DIR:${target}>")
    endif()
endfunction()
