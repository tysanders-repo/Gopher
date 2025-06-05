cmake_minimum_required(VERSION 3.10)
project(Gopher)

file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/src/*.h")
file(GLOB_RECURSE GLSL    "${CMAKE_SOURCE_DIR}/resources/*.glsl")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "All .cpp sources found:")
foreach(srcFile ${SOURCES})
    message(STATUS "  ${srcFile}")
endforeach()

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavutil
    libavformat
    libswscale
    libswresample
)

include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${GLSL}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    ${FFMPEG_LIBRARIES}
    "-framework AudioToolbox"
    "-framework VideoToolbox"
    "-framework CoreFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreAudio"
    "-lz"
    "-liconv"
)
