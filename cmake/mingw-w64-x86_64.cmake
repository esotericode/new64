# new64 -- cross-compile to 64-bit Windows with mingw-w64.
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-win -j
#
# On Debian/Ubuntu the prerequisites are:
#   apt install mingw-w64 libz-mingw-w64-dev
#
# The resulting executables are fully static: they import nothing beyond the
# DLLs that ship with Windows itself, so there is nothing to install alongside
# them.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

# Look for programs on the host, but headers and libraries only in the target
# sysroot -- otherwise CMake happily finds the host's Linux zlib.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Prefer static archives. Both libz.a and libz.dll.a exist in the sysroot, and
# picking the import library would make the executable need zlib1.dll at runtime.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
