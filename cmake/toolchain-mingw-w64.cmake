# CMake toolchain file for cross-compiling to x86_64 Windows via mingw-w64.
#
# Used by scripts/build-windows.sh inside docker/windows/Dockerfile
# (Arch Linux host with mingw-w64-gcc installed). Also works on any
# Linux distro whose mingw-w64 package puts the compilers on PATH as
# `x86_64-w64-mingw32-{gcc,g++,windres}`.
#
# Not used by the primary (host-native) build.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TRAILER_MINGW_TRIPLET x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TRAILER_MINGW_TRIPLET}-gcc)
set(CMAKE_CXX_COMPILER ${TRAILER_MINGW_TRIPLET}-g++)
set(CMAKE_RC_COMPILER  ${TRAILER_MINGW_TRIPLET}-windres)

# Point find_* at the cross sysroot, not the host's /usr. Callers
# append aqtinstall'd Qt and the prebuilt qpdf to CMAKE_PREFIX_PATH.
set(CMAKE_FIND_ROOT_PATH /usr/${TRAILER_MINGW_TRIPLET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
