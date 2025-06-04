CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

# Name of the project
PROJECT(Gopher)

# Use glob to get the list of all source files.
# We don't really need to include header and resource files to build, but it's
# nice to have them also show up in IDEs.
IF(${SOL})
	FILE(GLOB_RECURSE SOURCES "src0/*.cpp")
	FILE(GLOB_RECURSE HEADERS "src0/*.h")
	FILE(GLOB_RECURSE GLSL "resources0/*.glsl")
ELSE()
	FILE(GLOB_RECURSE SOURCES "src/*.cpp")
	FILE(GLOB_RECURSE HEADERS "src/*.h")
	FILE(GLOB_RECURSE GLSL "resources/*.glsl")
ENDIF()

# Generate compile_commands.json, used by Clangd, Clion, etc.
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set the executable.
ADD_EXECUTABLE(${CMAKE_PROJECT_NAME} ${SOURCES} ${HEADERS} ${GLSL})

# //////////////////////////////
# Tell CMake to find OpenCV (installed via Homebrew or from source)
find_package(OpenCV REQUIRED)  # this uses OpenCVConfig.cmake or `pkg-config`

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${OpenCV_LIBS})
# //////////////////////////////

# Use c++21
SET_TARGET_PROPERTIES(${CMAKE_PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
SET_TARGET_PROPERTIES(${CMAKE_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
