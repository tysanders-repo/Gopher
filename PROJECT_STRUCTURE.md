# Gopher Project Structure

This document describes the project structure and how to work with vendor dependencies.

## Directory Structure

```
gopher/
├── engine/                 # C++ static library (CMake)
│   ├── include/           # Public headers
│   ├── src/              # Source files
│   └── CMakeLists.txt    # Engine build configuration
├── apps/                 # tauri app
├── vendor/               # Third-party dependencies
│   ├── sdl2/            # SDL2/SDL3 library
│   │   ├── include/     # Headers
│   │   ├── lib/         # SDL3.framework
│   │   └── pkgconfig/   # sdl2.pc
│   └── ffmpeg/          # FFmpeg libraries
│       ├── include/     # Headers
│       ├── lib/         # Dynamic libraries (.dylib)
│       └── pkgconfig/   # *.pc files
├── bridge/               # Swift-C++ bridge
├── daemon/               # Background service
├── cmake/                # CMake utilities
└── CMakeLists.txt        # Root build configuration
```

## Vendor Dependencies

### SDL2/SDL3

- **Location**: `vendor/sdl2/`
- **Type**: XCFramework (SDL3.framework)
- **Architectures**: arm64 + x86_64 (universal binary)
- **Headers**: `vendor/sdl2/include/SDL3/`
- **Library**: `vendor/sdl2/lib/SDL3.framework`

The SDL3 framework was built from source using:
```bash
git clone https://github.com/libsdl-org/SDL.git
cd SDL
xcodebuild -project Xcode/SDL/SDL.xcodeproj \
  -scheme SDL3 -configuration Release \
  -arch arm64 -arch x86_64 build
```

### FFmpeg

- **Location**: `vendor/ffmpeg/`
- **Type**: Dynamic libraries (.dylib) from Homebrew
- **Headers**: `vendor/ffmpeg/include/`
- **Libraries**: `vendor/ffmpeg/lib/*.dylib`

FFmpeg was installed via Homebrew and copied to the vendor directory:
```bash
brew install ffmpeg
cp -R /opt/homebrew/include/* vendor/ffmpeg/include/
cp -R /opt/homebrew/lib/* vendor/ffmpeg/lib/
```

## Building

### C++ Engine Library

```bash
mkdir build && cd build
cmake ..
make
```

The engine library will be built as `libgopher_core.a` and will automatically link against the vendor dependencies.

### Swift App

Open `apps/MySwiftApp/macos.xcodeproj` in Xcode and build normally. The app can link against the built engine library.

## CMake Configuration

The root `CMakeLists.txt` automatically detects and configures vendor dependencies:

- **FFmpeg**: Creates `FFMPEG_interface` target linking against all FFmpeg libraries
- **SDL2**: Creates `SDL2::SDL2` target linking against the SDL3 framework

## Adding New Dependencies

To add a new vendor dependency:

1. Create directory structure: `vendor/newlib/{include,lib,pkgconfig}`
2. Copy headers to `include/`
3. Copy libraries to `lib/`
4. Create pkg-config files in `pkgconfig/`
5. Update root `CMakeLists.txt` to detect and configure the dependency

## Notes

- SDL3 is used instead of SDL2 (newer version with better macOS support)
- FFmpeg uses dynamic libraries for easier distribution
- All dependencies are self-contained within the project
- pkg-config files are provided for compatibility with existing build systems
