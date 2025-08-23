# Gopher - Professional Video Calling Framework

A highly reliable, fault-tolerant, low-latency video calling framework designed for satellite-based global software defined networks.

## 🏗️ Project Structure

```
gopher/
├── CMakeLists.txt                 # Drives C++ builds (core + daemon); can also emit Xcode projects
├── README.md                      # This file
├── docs/                          # Project documentation
│   ├── architecture.md            # System architecture overview
│   ├── build-macos.md             # macOS build instructions
│   └── ipc-protocol.md            # Inter-process communication protocol
├── cmake/                         # CMake helpers/toolchains
│   └── FindFFmpeg.cmake          # Custom FFmpeg finder
├── third_party/                   # Pinned external dependencies
├── core/                          # Main core utility library (C++)
│   ├── include/gopher/            # Public headers
│   │   ├── ffmpeg_receiver.hpp
│   │   ├── ffmpeg_sender.hpp
│   │   ├── gopher_client_lib.hpp
│   │   └── version.hpp
│   ├── src/                       # Implementation files
│   └── tests/                     # C++ unit tests
├── daemon/                        # Separate executable (C++)
│   ├── include/gopherd/           # Daemon headers
│   ├── src/                       # Daemon implementation
│   ├── launchd/                   # macOS launch agent
│   └── tests/                     # Daemon tests
├── bridge/                        # Obj-C++ shim for Swift integration
│   ├── include/gopher_bridge/     # C/ObjC headers
│   └── src/                       # Bridge implementation
├── apps/
│   └── macos-statusbar/           # Swift status bar app
│       ├── gopher.xcodeproj       # Xcode project
│       ├── Sources/               # Swift source files
│       ├── Resources/             # App resources
│       └── Tests/                 # Swift tests
└── test/                          # Cross-cutting system tests
    ├── e2e/                       # End-to-end tests
    └── data/                      # Test fixtures
```

## 🚀 Quick Start

### Prerequisites
- **macOS 12.0+**
- **Xcode 15.0+**
- **CMake 3.18+**
- **FFmpeg** (via Homebrew)
- **SDL2** (via Homebrew)

### Build C++ Components
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
sudo make install
```

### Build SwiftUI App
```bash
cd apps/macos-statusbar
open gopher.xcodeproj
# Build in Xcode
```

## 🏛️ Architecture

### Core Library
- **Pure C++** - No platform dependencies
- **FFmpeg integration** - High-performance video/audio
- **SDL2 integration** - Cross-platform media display
- **Thread-safe design** - Concurrent operation support

### Daemon
- **Background service** - macOS launchd integration
- **Peer discovery** - UDP broadcast and TCP query
- **Call routing** - Manages active connections
- **Health monitoring** - Self-healing capabilities

### Bridge Layer
- **C interface** - Clean C API for Swift
- **Error handling** - Comprehensive error codes
- **Memory management** - RAII-compliant C++ wrapper
- **Callback support** - Event-driven architecture

### SwiftUI App
- **Status bar integration** - Native macOS experience
- **Modern UI** - SwiftUI 5.0+ features
- **Network integration** - Network.framework usage
- **Media handling** - AVFoundation integration

## 🔧 Build System

### CMake Features
- **Multi-target builds** - Core, daemon, bridge
- **Dependency management** - FFmpeg, SDL2
- **Install targets** - System-wide installation
- **Xcode generation** - `cmake -G Xcode ..`

### Xcode Integration
- **Separate project** - SwiftUI app development
- **Library linking** - Links against built C++ libraries
- **Framework support** - Can use built bridge as framework

## 🧪 Testing

### Unit Tests
- **C++ tests** - GoogleTest framework
- **Swift tests** - XCTest framework
- **Integration tests** - Component interaction testing

### System Tests
- **E2E tests** - Full system validation
- **Performance tests** - Latency and throughput
- **Reliability tests** - Fault injection and recovery

## 📱 Features

### Video Calling
- **H.264 encoding** - Hardware-accelerated
- **Low latency** - Sub-50ms target
- **Adaptive quality** - Network condition adaptation
- **Multi-stream** - Video + audio synchronization

### Network
- **Peer discovery** - Automatic peer finding
- **Call routing** - Direct peer-to-peer
- **Fallback support** - Relay when needed
- **Global routing** - Satellite network ready

### Reliability
- **Fault tolerance** - Automatic recovery
- **Health monitoring** - Continuous health checks
- **Graceful degradation** - Service continuity
- **Disaster recovery** - Backup systems

## 🎯 Roadmap

See [docs/WISHLIST.md](docs/WISHLIST.md) for detailed feature roadmap and implementation priorities.

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Follow the established architecture
4. Add appropriate tests
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License. See [docs/LICENSE](docs/LICENSE) for details.

## 🆘 Support

- **Documentation**: Check the [docs/](docs/) directory
- **Issues**: Use GitHub issues for bug reports
- **Discussions**: Use GitHub discussions for questions
