cmake_minimum_required(VERSION 3.18)
project(Gopher VERSION 1.0.0 LANGUAGES CXX)

# ---- language / std
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- options
option(BUILD_TESTS  "Build tests"          ON)
option(BUILD_APPS   "Build applications"   ON)
option(BUILD_BRIDGE "Build Swift bridge"   ON)

# ---- output dirs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- dependencies
find_package(PkgConfig REQUIRED)

# FFmpeg via pkg-config (aggregates all sub-libs)
pkg_check_modules(FFMPEG REQUIRED
  libavdevice libavformat libavcodec libavutil libswscale libswresample
)

# Create an imported interface for FFmpeg usable across subdirs
add_library(FFMPEG::ffmpeg INTERFACE IMPORTED)
set_target_properties(FFMPEG::ffmpeg PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
  INTERFACE_LINK_LIBRARIES      "${FFMPEG_LIBRARIES}"
)

# SDL2: prefer config package which exports SDL2::SDL2 (Homebrew/macOS does)
# Falls back to FindSDL2.cmake variables if necessary.
find_package(SDL2 REQUIRED) # try CONFIG first; may also hit Module

# Normalize possible names (SDL2::SDL2 or SDL2::SDL2-static)
if(TARGET SDL2::SDL2)
  set(_SDL2_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
  set(_SDL2_TARGET SDL2::SDL2-static)
else()
  # Fallback for FindSDL2 that only sets variables
  add_library(SDL2::SDL2 INTERFACE IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES      "${SDL2_LIBRARIES}"
  )
  set(_SDL2_TARGET SDL2::SDL2)
endif()

# ---- debug info AFTER discovery (so vars are actually set)
message(STATUS "FFMPEG_INCLUDE_DIRS: ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFMPEG_LIBRARIES:    ${FFMPEG_LIBRARIES}")
message(STATUS "SDL2 target:          ${_SDL2_TARGET}")
message(STATUS "SDL2_INCLUDE_DIRS:    ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2_LIBRARIES:       ${SDL2_LIBRARIES}")

# ---- subdirectories (they can link FFMPEG::ffmpeg and ${_SDL2_TARGET})
add_subdirectory(core)
add_subdirectory(daemon)

if(BUILD_BRIDGE)
  add_subdirectory(bridge)
endif()

if(BUILD_APPS)
  add_subdirectory(apps)
endif()

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

# ---- installation
# Expect core/CMakeLists.txt to define target "gopher_core"
install(TARGETS gopher_core
  EXPORT GopherTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include/gopher
)

install(TARGETS gopherd
  RUNTIME DESTINATION bin
)

# Public headers (only the public API)
install(DIRECTORY core/include/gopher/
  DESTINATION include/gopher
  FILES_MATCHING PATTERN "*.hpp"
)

# ---- CMake package export (only export your own targets)
install(EXPORT GopherTargets
  FILE GopherTargets.cmake
  NAMESPACE Gopher::
  DESTINATION lib/cmake/Gopher
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/GopherConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/GopherConfigVersion.cmake
  DESTINATION lib/cmake/Gopher
)

# ---- Xcode niceties
if(CMAKE_GENERATOR STREQUAL "Xcode")
  set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
  set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++17")
endif()
