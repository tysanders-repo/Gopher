# Apps CMakeLists.txt
# This directory contains platform-specific applications

# For now, we'll just add a placeholder
# The macOS status bar app will be built separately via Xcode
# but can link against our built libraries

message(STATUS "Apps directory - macOS status bar app should be built via Xcode")
message(STATUS "Libraries can be linked from: ${CMAKE_BINARY_DIR}/lib")

# Optional: Add any command-line tools here
# add_subdirectory(command-line-tool)
