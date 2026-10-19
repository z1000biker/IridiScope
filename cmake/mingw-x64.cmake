# Cross-build using a MinGW-w64 POSIX compiler on Linux. Set CROSS_PREFIX to
# the triplet prefix (including its directory) and WINDOWS_DEPS to a MinGW
# prefix containing Windows headers/libraries, e.g. an MSYS2 mingw64 tree.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(NOT DEFINED CROSS_PREFIX)
  set(CROSS_PREFIX x86_64-w64-mingw32-)
endif()
set(CMAKE_C_COMPILER "${CROSS_PREFIX}gcc-posix")
set(CMAKE_CXX_COMPILER "${CROSS_PREFIX}g++-posix")
set(CMAKE_RC_COMPILER "${CROSS_PREFIX}windres")
set(CMAKE_AR "${CROSS_PREFIX}ar")
set(CMAKE_RANLIB "${CROSS_PREFIX}ranlib")
set(CMAKE_FIND_ROOT_PATH "${WINDOWS_DEPS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
