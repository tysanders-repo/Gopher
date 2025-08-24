# Core library CMakeLists.txt
set(CORE_SOURCES
  src/ffmpeg_receiver.cpp
  src/ffmpeg_sender.cpp
  src/gopher_client_lib.cpp
)

add_library(gopher_core STATIC ${CORE_SOURCES})

# Properties
set_target_properties(gopher_core PROPERTIES
  VERSION   ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/gopher/*.hpp"
)

# Public headers for consumers of gopher_core
target_include_directories(gopher_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# Don’t add src/ as PUBLIC includes; keep it private if you really need it:
# target_include_directories(gopher_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link to the imported targets defined in the root
target_link_libraries(gopher_core PUBLIC
  FFMPEG::FFMPEG
  SDL2::SDL2
)

# Compile defs/opts
target_compile_definitions(gopher_core PRIVATE SDL_MAIN_HANDLED)

# macOS frameworks needed by FFmpeg/SDL2 users
if(APPLE)
  target_link_libraries(gopher_core PUBLIC
    "-framework AudioToolbox"
    "-framework VideoToolbox"
    "-framework CoreFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreAudio"
  )
endif()

# Install target (use the same export set name as root)
# Note: install is handled by root CMakeLists.txt
