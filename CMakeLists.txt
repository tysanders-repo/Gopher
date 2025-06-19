cmake_minimum_required(VERSION 3.10)
project(Gopher)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
  libavdevice libavformat libavcodec libavutil libswscale libswresample
)
find_package(OpenCV REQUIRED)

# Common includes and flags
include_directories(
  ${FFMPEG_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/src
)
link_directories(${FFMPEG_LIBRARY_DIRS})

# === Gopher Daemon ===
file(GLOB_RECURSE DAEMON_SRC "src/gopherd.cpp")
add_executable(gopherd ${DAEMON_SRC})
target_link_libraries(gopherd PRIVATE
  ${OpenCV_LIBRARIES}
  ${FFMPEG_LIBRARIES}
)

# === Gopher Client ===
file(GLOB_RECURSE CLIENT_SRC "src/gopher_client.cpp")
add_executable(gopher_client ${CLIENT_SRC})
target_link_libraries(gopher_client PRIVATE
  ${OpenCV_LIBRARIES}
  ${FFMPEG_LIBRARIES}
)

# Optional macOS frameworks
if(APPLE)
  target_link_libraries(gopherd PRIVATE
    "-framework AudioToolbox"
    "-framework VideoToolbox"
    "-framework CoreFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreAudio"
    "-lz"
    "-liconv"
  )
  target_link_libraries(gopher_client PRIVATE
    "-framework AudioToolbox"
    "-framework VideoToolbox"
    "-framework CoreFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreAudio"
    "-lz"
    "-liconv"
  )
endif()
