# Gopher Project Structure

## Overview
This document describes the corrected project structure for the Gopher SwiftUI migration. The project has been reorganized with the Xcode project at the root level and proper file naming.

## Directory Structure

```
gopher/
├── gopher.xcodeproj/                 # Xcode project (at root level)
├── Sources/                           # C++ Core Components
│   ├── gopherd/                      # Daemon executable
│   │   ├── gopherd.cpp
│   │   └── gopherd.hpp
│   ├── gopher_client_lib/            # Core client library
│   │   ├── gopher_client_lib.cpp
│   │   ├── gopher_client_lib.hpp
│   │   └── gopher_client_pybind.cpp  # Python bindings (to be removed)
│   └── ffmpeg/                       # Media handling
│       ├── ffmpeg_sender.cpp
│       ├── ffmpeg_sender.hpp
│       ├── ffmpeg_receiver.cpp
│       └── ffmpeg_receiver.hpp
├── GopherApp.swift                   # Main SwiftUI app entry point
├── ContentView.swift                 # Main content view
├── Assets.xcassets/                  # App resources
├── gopher.entitlements              # App entitlements
├── gopherTests/                      # Unit tests
├── gopherUITests/                    # UI tests
├── Preview Content/                   # SwiftUI preview assets
├── Tests/                            # Test suites (empty, for future use)
│   ├── Unit/                         # Unit tests
│   ├── Integration/                  # Integration tests
│   └── UI/                          # UI tests
├── Shared/                           # Shared resources
│   ├── Resources/                    # Common resources
│   └── Config/                       # Configuration files
├── Build/                            # Build artifacts & scripts
│   ├── build.sh                      # New build script
│   ├── setup.py                      # Legacy Python setup (to be removed)
│   └── CMakeLists.txt               # Legacy CMake config (to be removed)
├── Documentation/                    # Project documentation
│   ├── README.md                     # Main project README
│   └── WISHLIST.md                  # Feature roadmap
└── bindings/                         # Legacy bindings (to be removed)
```

## Migration Status

### ✅ Completed
- [x] Project structure reorganization
- [x] Xcode project moved to root level
- [x] File naming corrected (GopherApp.swift)
- [x] Unused directories cleaned up
- [x] Basic SwiftUI app foundation
- [x] Status bar app configuration

### 🔄 In Progress
- [ ] C++ core integration with SwiftUI
- [ ] Network service implementation
- [ ] Media service implementation
- [ ] Call notification system

### ❌ Pending
- [ ] Remove Python dependencies
- [ ] Remove pybind11 bindings
- [ ] Remove CMake build system
- [ ] Remove Python apps
- [ ] Complete C++ bridge implementation

## Build System Changes

### Old System (Python + CMake)
- **Build Tool**: CMake + Python setup.py
- **Dependencies**: pybind11, py2app, rumps
- **Output**: Python app bundle with embedded C++ library

### New System (SwiftUI + Xcode)
- **Build Tool**: Xcode build system
- **Dependencies**: Swift Package Manager, native frameworks
- **Output**: Native macOS app bundle

## Next Steps

1. **Open the Xcode project**: `open gopher.xcodeproj`
2. **Build the app**: Use Xcode build system
3. **Implement C++ bridge**: Create Swift-C++ interface
4. **Migrate network code**: Replace Python sockets with Network.framework
5. **Migrate media code**: Integrate with existing FFmpeg pipeline

## Development Workflow

### For SwiftUI Development
```bash
open gopher.xcodeproj
# Use Xcode for development
```

### For C++ Development
```bash
cd Sources
# Edit C++ files, then rebuild in Xcode
```

### For Building
```bash
# Use Xcode build system directly
# Or create custom build scripts as needed
```

## Dependencies

### Required
- **Xcode 15.0+** (for SwiftUI 5.0+ features)
- **macOS 12.0+** (deployment target)
- **FFmpeg** (via Homebrew)
- **SDL2** (via Homebrew)

### Removed
- **Python 3.8+** (no longer needed)
- **pybind11** (replaced by Swift-C++ bridge)
- **py2app** (replaced by Xcode)
- **rumps** (replaced by SwiftUI)

## Notes

- The `gopher_client_pybind.cpp` file is kept temporarily for reference during migration
- The `setup.py` and `CMakeLists.txt` are moved to Build/ for reference
- The Xcode project is now properly located at the root level
- All SwiftUI development should use the main project files
- The app will display as "Gopher" to users despite the project name
