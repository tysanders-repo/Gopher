# Daemon executable CMakeLists.txt
set(DAEMON_SOURCES
  src/gopherd.cpp
)

# Create daemon executable
add_executable(gopherd ${DAEMON_SOURCES})

# Link with core library
target_link_libraries(gopherd PRIVATE gopher_core ${FFMPEG_LIBRARIES})

# Set library search path
target_link_directories(gopherd PRIVATE ${FFMPEG_LIBRARY_DIRS})

# Include directories
target_include_directories(gopherd PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# macOS specific settings
if(APPLE)
  target_link_libraries(gopherd PRIVATE
    "-framework AudioToolbox"
    "-framework VideoToolbox"
    "-framework CoreFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreAudio"
  )
endif()

# Install daemon
install(TARGETS gopherd
  RUNTIME DESTINATION bin
)

# Install launchd plist if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/launchd/com.yourorg.gopherd.plist")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/launchd/com.yourorg.gopherd.plist"
    DESTINATION /Library/LaunchDaemons
  )
endif()
