# Core library CMakeLists.txt
set(CORE_SOURCES
  src/ffmpeg_receiver.cpp
  src/ffmpeg_sender.cpp
  src/gopher_client_lib.cpp
)

# Create core library
add_library(gopher_core STATIC ${CORE_SOURCES})


# Set target properties
set_target_properties(gopher_core PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/gopher/*.hpp"
)

# Include directories
target_include_directories(gopher_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
  ${FFMPEG_INCLUDE_DIRS}
  ${SDL2_INCLUDE_DIRS}
)

# find dependencies
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
  libavdevice libavformat libavcodec libavutil libswscale libswresample
)

target_link_libraries(gopher_core PUBLIC
  SDL2::SDL2
  SDL2::SDL2main
  ${FFMPEG_LIBRARIES}
)

# Compiler flags
target_compile_options(gopher_core PRIVATE
  -DSDL_MAIN_HANDLED
)

# macOS specific settings
if(APPLE)
  target_link_libraries(gopher_core PRIVATE
    "-framework AudioToolbox"
    "-framework VideoToolbox"
    "-framework CoreFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreAudio"
  )
endif()

# Install target
install(TARGETS gopher_core
  EXPORT GopherCoreTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include/gopher
)
