cmake_minimum_required(VERSION 3.10)
project(Gopher)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# FetchContent for pybind11 if not installed
include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v2.11.0
)
FetchContent_MakeAvailable(pybind11)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
  libavdevice libavformat libavcodec libavutil libswscale libswresample
)
find_package(SDL2 REQUIRED)
find_package(OpenCV REQUIRED)

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS")
endif()


include_directories(
  ${FFMPEG_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
  ${SDL2_INCLUDE_DIRS}
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

# === Gopher Client Library ===
file(GLOB_RECURSE CLIENT_LIB_SRC
    "src/gopher_client_lib.cpp"
    "src/ffmpeg_sender.cpp"
    "src/ffmpeg_receiver.cpp"
)
add_library(gopher_client_lib STATIC ${CLIENT_LIB_SRC})
target_link_libraries(gopher_client_lib PUBLIC
  ${FFMPEG_LIBRARIES}
  ${OpenCV_LIBRARIES}
  ${SDL2_LIBRARIES}
)
target_include_directories(gopher_client_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)

# === Python Extension Module via pybind11 ===
# Name the module 'gopher_client' so Python 'import gopher_client' works
pybind11_add_module(gopher_client
    src/gopher_client_pybind.cpp
)
target_link_libraries(gopher_client PRIVATE
    gopher_client_lib
)
target_include_directories(gopher_client PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Optional macOS frameworks
if(APPLE)
  foreach(target IN ITEMS gopherd gopher_client_lib gopher_client)
    target_link_libraries(${target} PRIVATE
      "-framework AudioToolbox"
      "-framework VideoToolbox"
      "-framework CoreFoundation"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework CoreAudio"
      "-lz"
      "-liconv"
    )
  endforeach()
endif()
