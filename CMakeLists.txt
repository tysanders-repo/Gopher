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

# ---- vendor dependencies
set(VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor")
set(SDL2_INC   "${VENDOR_DIR}/sdl2/include")
# Use architecture-specific FFmpeg path
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(FFMPEG_INC "${VENDOR_DIR}/ffmpeg/arm64/include")
  set(FFMPEG_LIB_DIR "${VENDOR_DIR}/ffmpeg/arm64/lib")
else()
  set(FFMPEG_INC "${VENDOR_DIR}/ffmpeg/x86_64/include")
  set(FFMPEG_LIB_DIR "${VENDOR_DIR}/ffmpeg/x86_64/lib")
endif()

# ---- FFmpeg (vendor) as one INTERFACE that carries includes + all dylibs
if(EXISTS "${VENDOR_DIR}/ffmpeg")
  message(STATUS "Using FFmpeg from vendor: ${VENDOR_DIR}/ffmpeg")
  add_library(FFMPEG::FFMPEG INTERFACE IMPORTED GLOBAL)
  set_target_properties(FFMPEG::FFMPEG PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INC}"  # ABSOLUTE
    INTERFACE_LINK_LIBRARIES "${FFMPEG_LIB_DIR}/libavcodec.dylib;${FFMPEG_LIB_DIR}/libavformat.dylib;${FFMPEG_LIB_DIR}/libavutil.dylib;${FFMPEG_LIB_DIR}/libswscale.dylib;${FFMPEG_LIB_DIR}/libswresample.dylib;${FFMPEG_LIB_DIR}/libavdevice.dylib"
  )
else()
  message(FATAL_ERROR "FFmpeg not found at ${VENDOR_DIR}/ffmpeg")
endif()

# ---- SDL2 (vendor) as an imported framework
if(EXISTS "${VENDOR_DIR}/sdl2/lib/SDL2.framework/SDL2")
  message(STATUS "Using SDL2 from vendor: ${VENDOR_DIR}/sdl2")
  add_library(SDL2::SDL2 SHARED IMPORTED GLOBAL)
  set_target_properties(SDL2::SDL2 PROPERTIES
    FRAMEWORK TRUE
    IMPORTED_LOCATION "${VENDOR_DIR}/sdl2/lib/SDL2.framework/SDL2"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INC}"     # ABSOLUTE
  )
else()
  message(FATAL_ERROR "SDL2 not found at ${VENDOR_DIR}/sdl2")
endif()

message(STATUS "FFMPEG_INC: ${FFMPEG_INC}")
message(STATUS "SDL2_INC:   ${SDL2_INC}")

# ---- subdirectories
add_subdirectory(engine)
add_subdirectory(daemon)

if(BUILD_BRIDGE)
  add_subdirectory(bridge)
endif()

# Apps now handled by Tauri in the 'app' directory
# if(BUILD_APPS)
#   add_subdirectory(apps)
# endif()

if(BUILD_TESTS)
  enable_testing()
  # Tests directory is empty for now
  # add_subdirectory(engine/tests)
endif()

# ---- installation/exports (use a single export set name)
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

install(DIRECTORY engine/include/gopher/
  DESTINATION include/gopher
  FILES_MATCHING PATTERN "*.hpp"
)

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

# ---- Xcode niceties (optional)
if(CMAKE_GENERATOR STREQUAL "Xcode")
  set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
  set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++17")
endif()
