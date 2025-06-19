#!/bin/bash
# scripts/build.sh - Cross-platform build script

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     MACHINE=Linux;;
    Darwin*)    MACHINE=Mac;;
    CYGWIN*)    MACHINE=Cygwin;;
    MINGW*)     MACHINE=MinGw;;
    *)          MACHINE="UNKNOWN:${OS}"
esac

echo -e "${GREEN}Building Gopher for ${MACHINE}...${NC}"

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check dependencies
echo -e "${YELLOW}Checking dependencies...${NC}"

if ! command_exists cmake; then
    echo -e "${RED}Error: cmake is not installed${NC}"
    exit 1
fi

if ! command_exists pkg-config; then
    echo -e "${RED}Error: pkg-config is not installed${NC}"
    exit 1
fi

# Platform-specific dependency checks
if [[ "$MACHINE" == "Mac" ]]; then
    if ! command_exists brew; then
        echo -e "${RED}Error: Homebrew is not installed${NC}"
        exit 1
    fi
    
    # Check for OpenCV and FFmpeg
    if ! brew list opencv >/dev/null 2>&1; then
        echo -e "${YELLOW}Installing OpenCV...${NC}"
        brew install opencv
    fi
    
    if ! brew list ffmpeg >/dev/null 2>&1; then
        echo -e "${YELLOW}Installing FFmpeg...${NC}"
        brew install ffmpeg
    fi
elif [[ "$MACHINE" == "Linux" ]]; then
    # Check for required packages
    if ! pkg-config --exists opencv4 && ! pkg-config --exists opencv; then
        echo -e "${RED}Error: OpenCV development libraries not found${NC}"
        echo "Please install: sudo apt-get install libopencv-dev"
        exit 1
    fi
    
    if ! pkg-config --exists libavcodec; then
        echo -e "${RED}Error: FFmpeg development libraries not found${NC}"
        echo "Please install: sudo apt-get install libavdevice-dev libavformat-dev libavcodec-dev"
        exit 1
    fi
fi

# Create build directory
BUILD_DIR="build"
if [[ -d "$BUILD_DIR" ]]; then
    echo -e "${YELLOW}Cleaning existing build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo -e "${YELLOW}Configuring build...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo -e "${YELLOW}Building...${NC}"
if [[ "$MACHINE" == "Mac" ]]; then
    make -j$(sysctl -n hw.ncpu)
else
    make -j$(nproc)
fi

# Success message
echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}Executables are in: ${BUILD_DIR}/${NC}"
echo -e "  - gopher_client"
echo -e "  - gopherd"

# Optional: Run tests if they exist
if [[ -f "Makefile" ]] && make -n test >/dev/null 2>&1; then
    echo -e "${YELLOW}Running tests...${NC}"
    make test
fi
