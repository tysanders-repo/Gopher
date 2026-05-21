# Gopher

Peer-to-peer video calling with sub-50ms glass-to-glass latency and a "tap to call" UX.
End-to-end encrypted from day one. LAN works with zero infrastructure.

## Architecture

```
gopher/
├── app/          # Tauri desktop shell (Rust + WKWebView/WebView2)
├── engine/       # C++ media core (H.264, Opus, UDP transport)
│   ├── include/  # Public headers
│   └── src/      # FFmpegSender, FFmpegReceiver, GopherClient
├── daemon/       # gopherd — LAN discovery via UDP broadcast + mDNS
├── bridge/       # Obj-C++ shim exposing the C++ engine to Rust via C ABI
└── vendor/       # Pinned FFmpeg and SDL2 binaries
```

### Key components

| Layer | Technology |
|-------|-----------|
| UI shell | Tauri (Rust + WKWebView on macOS, WebView2 on Windows) |
| Video encode | H.264 via `h264_videotoolbox` (HW) or libx264 (SW fallback) |
| Video decode | `AVSampleBufferDisplayLayer` + `VTDecompressionSession` |
| Audio | Opus |
| Transport | UDP, RTP-style 24-byte header + ChaCha20-Poly1305 AEAD |
| Discovery | mDNS + UDP broadcast (`gopherd`) |
| Signaling | TCP line-delimited JSON, peer-to-peer |
| Crypto | libsodium (AEAD), `snow` (Noise IK handshake), Ed25519 identity |
| C++ ↔ Rust | C bridge (`bridge/`) + bindgen |

## Building

### Prerequisites

- macOS 14+ (Sequoia) — Windows support coming in v0.2
- Xcode 16+
- CMake 3.18+
- Rust (stable) + `cargo-tauri` (`cargo install tauri-cli`)
- Node.js 20+

Vendored FFmpeg and SDL2 are checked in under `vendor/` — no Homebrew required.

### C++ engine

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(sysctl -n hw.ncpu)
```

### Tauri app (engine + UI together)

```bash
cd app
npm install
cargo tauri dev
```

## Running tests

```bash
ctest --test-dir build --output-on-failure
```

### Dual-peer integration test

`scripts/dual-peer` launches two engine instances against each other, prefixes
their output `[A]` / `[B]`, and cleans up both on Ctrl+C:

```bash
scripts/dual-peer --test-pattern     # synthetic lavfi sources — no camera needed
scripts/dual-peer                    # real cameras (peer A → device 0:, peer B → device 1:)
scripts/dual-peer --ports 51200 51201
```

The `--test-pattern` mode is the one that works in headless / CI / non-interactive
contexts — AVFoundation cameras need an active `NSRunLoop` and TCC permission
that aren't available in those environments.

## Status

Working toward **M0 — Foundation** (week of 2026-05-25):
no globals in the engine, CI green, one README that matches reality.

See `docs/BUILD_PLAN.md` for the full milestone roadmap.
