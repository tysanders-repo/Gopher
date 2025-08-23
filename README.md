**Gopher Video Calling Suite**

A lightweight peer-to-peer video calling framework leveraging FFmpeg, SDL2, and Python for discovery and UI on macOS.

---

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Overview](#overview)
- [Architecture](#architecture)
- [Components](#components)
- [Prerequisites](#prerequisites)
- [Building and Installation](#building-and-installation)
- [Configuration](#configuration)
- [Usage](#usage)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

The Gopher Video Calling Suite provides:

* **Peer discovery** via UDP broadcasts and a central daemon (`gopherd`).
* **Status bar UI** on macOS (`gopher_status_app.py`) for initiating and managing calls.
* **High-performance video streaming** using FFmpeg for encoding/decoding.
* **C++ core** (`GopherClient`) exposed to Python via pybind11.
* **SDL2-based display** of incoming video, compliant with macOS threading requirements.

Ideal for lightweight local network video calls without centralized servers.

---

## Architecture

```
+--------------+        +------------+        +----------------+
| Status App   | <-->   | gopherd    | <-->   | Status App     |
| (Python+UI)  | UDP    | (Daemon)   | TCP    | (Python+UI)    |
+--------------+        +------------+        +----------------+
      |                                    |
      v                                    v
+-------------+   UDP   +-------------+   UDP   +-------------+
| FFmpegSender| ------> | FFmpegReceiver| <-----| FFmpegSender|
| (C++)       |         | (C++)        |        | (C++)       |
+-------------+         +-------------+        +-------------+
      \                                     /
       \                                   /
        \---process_video_display (SDL2)--/    (Python)
```

* **Discovery**: `gopher_status_app.py` broadcasts presence, queries `gopherd` for peers.
* **Call Setup**: Status app spawns `gopher_video_app.py`, which initializes `GopherClient` in both sender and receiver modes.
* **Media Pipeline**: `FFmpegSender` captures camera input, encodes to H.264 (low-latency), and sends via UDP. `FFmpegReceiver` decodes incoming packets and enqueues frames.
* **Display**: Frames are rendered in an SDL2 window on the main thread by calling into the C++ display loop from Python.

---

## Components

1. **gopherd** (C++)

   * UDP peer listener (port 43753)
   * TCP query server (port 43823)
2. **GopherClient** (C++)

   * Core API for call control, discovery integration, and media threads.
   * Exposed to Python via **pybind11**.
3. **FFmpegSender/Receiver** (C++)

   * Real-time capture, encoding, and decoding using FFmpeg.
   * Adaptive throttling and frame-drop logic for smooth streaming.
4. **gopher\_status\_app.py** (Python)

   * Mac status-bar UI via **rumps**. Peer listing, call initiation, menu refresh.
5. **gopher\_video\_app.py** (Python)

   * Standalone video display client.
   * Handles signals and clean shutdown via `GopherClient` prompts.

---

## Prerequisites

* **macOS 10.15+**
* **C++17** compiler (Clang or GCC)
* **CMake** ≥ 3.18
* **FFmpeg** development libraries (`libavcodec`, `libavformat`, `libavdevice`, `libswscale`)
* **SDL2** development headers
* **pybind11**
* **Python 3.8+**
* Python packages: `rumps`, `pybind11`, `typing_extensions`
* **Xcode** command-line tools

---

## Building and Installation

1. **Clone & submodules**

   ```sh
   git clone https://github.com/yourorg/gopher-video-suite.git
   cd gopher-video-suite
   git submodule update --init --recursive
   ```

2. **Build C++ core and FFmpeg modules**

   ```sh
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j$(sysctl -n hw.ncpu)
   sudo make install
   ```

3. **Install Python components**

   ```sh
   python3 -m venv venv
   source venv/bin/activate
   pip install -r requirements.txt
   ```

4. **Install macOS UI tool** (if not in `$PATH`)

   ```sh
   ln -sf $(pwd)/scripts/gopher_status_app.py /usr/local/bin/gopher-status
   ln -sf $(pwd)/scripts/gopher_video_app.py  /usr/local/bin/gopher-video
   ```

---

## Configuration

* **Dev Mode**: Pass `--dev` to both Python apps to include self in peer list and enable verbose logging.
* **Ports**: Default broadcast port 43753 and query port 43823 can be overridden via constants in `gopherd.cpp`.

---

## Usage

1. **Start the daemon**

   ```sh
   sudo gopherd
   ```

2. **Launch Status App**

   ```sh
   gopher-status --name alice
   ```

3. **Select a peer from the menu** to initiate a call.

4. **End Call** via status-menu or close the video window.

---

## Troubleshooting

* **No peers listed**: Ensure `gopherd` is running and UDP broadcast is allowed in firewall.
* **Video window black/stuck**: Check that camera access is granted (add `NSCameraUseContinuityCameraDeviceType` to Info.plist for AVCapture).
* **High latency or frame drops**: Adjust encoder settings in `FFmpegSender` (bitrate, GOP size) or network MTU.

---

## Contributing

1. Fork the repo
2. Create a feature branch (`git checkout -b feat/your-feature`)
3. Commit your changes
4. Submit a Pull Request

Please adhere to the existing style and write tests for new features.

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
