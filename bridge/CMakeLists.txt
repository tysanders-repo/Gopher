# Bridge library CMakeLists.txt
set(BRIDGE_SOURCES
  src/gopher_bridge.mm
)

# Create bridge library
add_library(gopher_bridge STATIC ${BRIDGE_SOURCES})

# Set target properties
set_target_properties(gopher_bridge PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/gopher_bridge/*.h"
)

# Include directories
target_include_directories(gopher_bridge PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${FFMPEG_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/core/include
)

# Link with core library
target_link_libraries(gopher_bridge PRIVATE gopher_core)

# macOS specific settings
if(APPLE)
  target_link_libraries(gopher_bridge PRIVATE
    "-framework Foundation"
    "-framework CoreFoundation"
    "-framework AudioToolbox"
    "-framework VideoToolbox"
  )
  
  # Set Objective-C++ standard
  set_target_properties(gopher_bridge PROPERTIES
    COMPILE_FLAGS "-x objective-c++"
  )
endif()

# Install target
install(TARGETS gopher_bridge
  EXPORT GopherBridgeTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include/gopher_bridge
)
