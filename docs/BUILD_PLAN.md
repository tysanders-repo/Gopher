# Gopher — Build Plan

**Author:** ty
**Date:** 2026-05-19
**Target:** macOS LAN alpha by end of August, public beta on macOS+Windows by mid-October.

---

## 1. Mission

Gopher is a peer-to-peer video calling app focused on **sub-50ms glass-to-glass latency** and a **frictionless "tap to call" UX**. End-to-end encrypted from v0.1. macOS first, Windows second. No mandatory servers — LAN works with zero infrastructure; WAN uses optional self-hostable rendezvous and relay (Magic Wormhole model).

**Tagline:** *Calling like Magic Wormhole is file transfer.*

## 2. In scope / out of scope

**In scope, v1:**
- Tauri shell + native video window (NSWindow on macOS, HWND on Windows)
- Hardware-accelerated H.264 video, Opus audio
- LAN discovery via UDP broadcast + mDNS
- TCP signaling between peers (INVITE/RINGING/ACCEPT/REJECT/BYE/NACK)
- Noise IK handshake + ChaCha20-Poly1305 encrypted media
- Ed25519 long-term identity keys stored in OS keychain
- Per-frame RTP-style packetization with fragment reassembly
- "Meet in 5?" scheduled invite
- macOS .dmg, code-signed, notarized, auto-updating
- Windows .msi, code-signed, auto-updating

**Out of scope, explicitly deferred:**
- ~~Satellite networking~~ — deleted from vision
- ~~SDN / programmable network~~ — deleted from vision
- ~~Quantum-resistant cryptography~~ — deleted from vision
- ~~6G integration~~ — deleted from vision
- Group calls (3+ participants) — v2
- Mobile (iOS/Android) — v2
- Screen sharing — v2
- Text chat — v2
- AI features (background blur, etc.) — v2

## 3. Architecture commitments

- **UI framework:** Tauri (Rust + WKWebView/WebView2)
- **Video render:** Native NSWindow child of Tauri main window on macOS; HWND on Windows. `AVSampleBufferDisplayLayer` + `VTDecompressionSession` for zero-copy on macOS.
- **Engine:** Existing C++ library, refactored to remove global state. Exposed to Rust via C bridge + bindgen.
- **Discovery (LAN):** mDNS + UDP broadcast (existing `gopherd`, extended)
- **Signaling:** TCP line-delimited JSON, peer-to-peer
- **Media transport:** UDP, RTP-style 24-byte header + ChaCha20-Poly1305 AEAD
- **Crypto:** libsodium for AEAD; `snow` crate for Noise IK handshake; Ed25519 identity in macOS Keychain / Windows DPAPI
- **WAN traversal (v0.2):** STUN for external address discovery, hole punching, optional self-hosted relay binary
- **FFmpeg:** Statically linked, stripped down (h264 decode + h264_videotoolbox encode + opus + UDP only)

## 4. Milestones

Each milestone has a **Definition of Done (DoD)** that must be met before moving on.

### M0 — Foundation cleanup (Week of May 25)
**Goal:** Honest baseline. One README, no globals, CI building.

- Delete `docs/WISHLIST.md` and `docs/PROJECT_STRUCTURE.md` (stale + over-promising)
- Rewrite root `README.md` to reflect actual Tauri-based architecture
- Move global mutable state in `engine/` into `GopherClient` member variables: `display_mutex`, `frame_queue`, `send_thread_should_stop_`, `recv_thread_should_stop_`, `main_thread_should_stop_`, `input_ctx`, `gopher_client*`, `video_width/height`
- Strip dead Python/SwiftUI references from code and docs
- Set up GitHub Actions: build + run unit tests on macos-14 runner
- Add `clang-format` and `rustfmt` to CI

**DoD:** `cargo tauri dev` and `cmake --build` both succeed from a fresh clone with no warnings. CI green. One README that matches reality.

### M1 — Fix the wire (Weeks of Jun 1, Jun 8)
**Goal:** Correct, measurable, recoverable media transport.

- Define `PacketHeader` struct, 24 bytes: version, type, stream_id, frame_id, frag_id, frag_count, capture_ts_us, payload_len
- Replace 4-call `sendto` pattern with single-datagram-per-fragment in `FFmpegSender::sendPacket`
- Implement reassembly buffer in `FFmpegReceiver`: `std::map<uint32_t, PendingFrame>` keyed by frame_id, max 8 entries, evict oldest
- Drop incomplete frames after 50ms timeout
- Hand complete H.264 access units to decoder; verify with `h264_analyze`
- Log per-frame metrics to CSV: `frame_id, send_ts, recv_ts, decode_ts, frag_loss, complete`
- Test under `dnctl pipe` / `pfctl` with 1% loss + 50ms jitter to verify reassembly works
- Bump `SO_RCVBUF` and `SO_SNDBUF` to 4MB

**DoD:** A 60-second call between two macs on the same network produces a CSV with P50 end-to-end latency under 40ms and zero corrupt frames at 0% loss, graceful frame drop (no decoder crashes) at 5% simulated loss.

### M2 — Bridge to Tauri end-to-end (Week of Jun 15)
**Goal:** Buttons in the UI actually drive the C++ engine.

- Add `bindgen` to `app/src-tauri/build.rs`, generate Rust bindings from `bridge/include/gopher_bridge/gopher_bridge.h`
- Wrap unsafe bindings in safe `gopher` Rust module with RAII handle
- Replace `static mut BROADCASTER` with `tauri::State<Mutex<GopherEngine>>`
- Wire `initialize_gopher` Tauri command to `gopher_client_create`
- Wire `start_call` Tauri command to `gopher_client_start_call` (real, not stub)
- Implement `gopher_client_set_incoming_call_callback` properly in bridge: callback into Rust, Rust emits Tauri event, JS shows notification

**DoD:** Two macs on the same network: launch app on both, name yourself, see the other in the peer list, click Call, video appears in an SDL window. End-to-end real call driven by UI clicks.

### M3 — Signaling protocol (Weeks of Jun 22, Jun 29)
**Goal:** Ring before you send. Accept/decline before you receive.

- TCP signaling listener per peer on port 43824
- JSON line-delimited message format
- Message types: `INVITE`, `RINGING`, `ACCEPT`, `REJECT`, `BYE`, `PING`, `PONG`, `NACK`
- State machine: `IDLE → CALLING → IN_CALL → IDLE` and `IDLE → RINGING → IN_CALL → IDLE`
- Rewire UI flow: clicking Call no longer immediately starts media — sends `INVITE`, shows "calling…" UI, waits for `ACCEPT`
- Incoming `INVITE` triggers system notification with Accept/Decline buttons via `tauri-plugin-notification`
- Implement `BYE` for clean hangup
- `PING`/`PONG` every 2 seconds, hang up call if no `PONG` for 10s (connection dead)
- "Meet in 5?" feature: `INVITE` with `scheduled_at` field; recipient sees scheduling toast

**DoD:** Calls only start after acceptance. Declining a call doesn't start media. Hanging up cleanly tears down on both sides. Dropping Wi-Fi mid-call detects within 12s and shows a "connection lost" message.

### M4 — Crypto (Weeks of Jul 6, Jul 13)
**Goal:** Nobody else can see the call. Identity is cryptographic, not nominal.

- Generate Ed25519 long-term keypair on first launch
- Store private key in macOS Keychain via `security-framework` crate
- Display identity as base32-encoded pubkey fingerprint in UI (12-char short form)
- Add pubkey to UDP broadcast announce so peers can address each other by key
- Implement Noise IK handshake on top of TCP signaling using `snow` crate
- After handshake, derive two ChaCha20-Poly1305 keys (send + recv)
- Encrypt every media datagram: AAD = 24-byte header, nonce = frame_id‖frag_id, tag = 16 bytes appended
- Verify with Wireshark: nothing in plaintext except headers
- Add "Trust on First Use" handling for new peer identities
- Add manual key-exchange flow for first-contact: SPAKE2 over short code (defer if time-pressed; TOFU is acceptable for v0.1)

**DoD:** Wireshark capture of a call shows ChaCha20-Poly1305 ciphertext for media and a Noise handshake on TCP. A man-in-the-middle attacker can't decrypt. Switching to a new computer asks me to re-trust my own identity (key in keychain).

### M5 — Audio (Weeks of Jul 20, Jul 27)
**Goal:** Hear them. Lip-sync.

- Add second `AVCaptureSession` audio input on macOS (mic)
- Opus encoder via libopus (statically linked)
- Audio packets use `type=2`, share the same packet header and crypto
- Receiver demuxes by type into separate jitter buffers (20-60ms for audio, smaller for video)
- A/V sync via `capture_ts_us` field: hold video until matching audio ready (or vice versa, within sync window)
- Audio playback via AVAudioEngine or CoreAudio
- Mute button in UI (stop encoding/sending audio, but keep stream alive)
- Audio level meter in UI

**DoD:** Two-person call with audio + video, lip-sync visually correct (within one frame), no echo with default mic, mute toggles within 100ms.

### M6 — Native video window + zero-copy path (Weeks of Aug 3, Aug 10)
**Goal:** The 50ms target. Glass to glass.

- Replace `avcodec_find_decoder(AV_CODEC_ID_H264)` software decoder with `VTDecompressionSession` directly (or FFmpeg's `h264_videotoolbox` decoder with hw_device_ctx)
- Decoder outputs `CVPixelBuffer` backed by IOSurface (no CPU copies)
- Create native `NSWindow` from Rust via `objc2` crate
- Window hosts `AVSampleBufferDisplayLayer`
- Feed decoded `CMSampleBuffer`s straight to display layer
- Wire as child window of Tauri main window via `addChildWindow:ordered:`
- Window position tracks main window; close hides; reopens on new call
- Delete SDL2 from the build
- On the send side: bypass `avformat_open_input` for camera; use `AVCaptureSession` + `VTCompressionSession` directly to avoid `sws_scale` color conversion
- Benchmark: log encode time, decode time, glass-to-glass

**DoD:** P50 glass-to-glass latency under 40ms on LAN between two M-series Macs. P99 under 80ms. CPU usage under 15% per call on a base M-series. SDL2 no longer linked.

### M7 — Polish + macOS private alpha (Week of Aug 17)
**Goal:** First demo-able artifact. Portfolio-grade.

- In-call control bar: mute, camera toggle, hangup
- Contact list: persistent storage of trusted pubkeys with friendly names
- Settings: change display name, view your identity fingerprint
- macOS Info.plist: `NSCameraUsageDescription`, `NSMicrophoneUsageDescription`
- Hardened runtime entitlements
- App icon design (use a placeholder if needed — don't lose a day here)
- Code-sign with Developer ID Application
- Notarize and staple
- Build `.dmg` with `tauri build`
- Wire `tauri-plugin-updater` (manifest hosted on GitHub releases or your own bucket)
- Ship to 3 trusted alpha testers (you, dad, one friend)
- Record a 60-second demo video for portfolio

**DoD:** Three people can install the `.dmg`, see each other, and have an encrypted call. Demo video recorded. App relaunches itself on update.

### Buffer week (Week of Aug 24)
**Goal:** Slack for milestone slip (statistically certain). Polish demo. Job application push.

If milestones held: focus on job applications, polish README, write a launch blog post draft.
If milestones slipped: catch-up week.

### M8 — Windows port (Weeks of Aug 31, Sep 7, Sep 14)
**Goal:** Same app, second platform.

- Replace `avfoundation` capture with Media Foundation (`IMFSourceReader`)
- Replace `VideoToolbox` encode/decode with Media Foundation H.264 or NVENC/QuickSync
- Replace `AVSampleBufferDisplayLayer` with D3D11 swap chain + `ID3D11VideoProcessor`
- HWND-based native video window via `windows-rs`
- Replace POSIX sockets with Winsock2 (engine already has `#ifdef _WIN32` scaffolding)
- Replace macOS Keychain with Windows DPAPI for identity key storage
- Test on Windows 10 and Windows 11
- Code-sign with Authenticode cert (acquire if not owned)
- Build `.msi` installer via Tauri's WiX integration
- Test interop: macOS ↔ Windows call

**DoD:** Windows build runs, calls between Windows and macOS work. Latency target met on Windows (may need separate tuning).

### M9 — WAN: NAT traversal + relay (Weeks of Sep 21, Sep 28, Oct 5)
**Goal:** Calls outside the LAN.

- STUN client for external address discovery (use Google's `stun.l.google.com:19302` or run your own)
- ICE-style hole punching: exchange external addresses in `INVITE`/`ACCEPT`, both sides spam UDP at each other to open NAT pinholes
- Build `gopher-rendezvous` service (small Rust binary) — federates peer announces across networks
- Build `gopher-relay` service (small Rust binary) — forwards encrypted UDP between two peers when direct fails
- Spin up `relay.gopher.app` on a $10 Hetzner VPS
- Client detects direct-connection failure within 5 seconds, falls back to relay
- Document how to self-host both services (this is the Geerling-blog moment — write a "run your own gopher relay" post)

**DoD:** Calls across two different home networks work. Connection upgrades from relay to direct silently if hole punching succeeds during the call.

### M10 — Public beta launch (Week of Oct 12)
**Goal:** Strangers download it.

- Landing page: one page, video demo, download buttons, source link
- Open-source the repo (MIT or Apache 2.0)
- Hacker News / Lobsters / r/programming launch posts drafted in advance
- Crash reporting (sentry-rust or similar, opt-in)
- Privacy policy (one paragraph: we collect nothing; relay forwards encrypted bytes only)

**DoD:** App on the website, source on GitHub, launch posts live. First 100 downloads, no embarrassing crash reports.

## 5. Calendar

| Week starting | Milestone | Hours est. |
|---|---|---|
| May 25 | M0 — Cleanup | 15 |
| Jun 1  | M1 — Wire (1/2) | 20 |
| Jun 8  | M1 — Wire (2/2) | 20 |
| Jun 15 | M2 — Bridge wiring | 18 |
| Jun 22 | M3 — Signaling (1/2) | 20 |
| Jun 29 | M3 — Signaling (2/2) | 20 |
| Jul 6  | M4 — Crypto (1/2) | 20 |
| Jul 13 | M4 — Crypto (2/2) | 20 |
| Jul 20 | M5 — Audio (1/2) | 20 |
| Jul 27 | M5 — Audio (2/2) | 20 |
| Aug 3  | M6 — Native video (1/2) | 22 |
| Aug 10 | M6 — Native video (2/2) | 22 |
| Aug 17 | M7 — macOS alpha | 18 |
| Aug 24 | Buffer / job apps | — |
| Aug 31 | M8 — Windows (1/3) | 20 |
| Sep 7  | M8 — Windows (2/3) | 20 |
| Sep 14 | M8 — Windows (3/3) | 20 |
| Sep 21 | M9 — WAN (1/3) | 20 |
| Sep 28 | M9 — WAN (2/3) | 20 |
| Oct 5  | M9 — WAN (3/3) | 20 |
| Oct 12 | M10 — Public beta | 18 |

**Total estimate:** ~370 hours over 20 weeks. Assumes ~18-20 productive hours/week.

**Critical demo dates:**
- **Aug 23:** macOS LAN alpha exists, demo video recorded — *the artifact you point hiring managers at*
- **Sep 20:** Windows + macOS interop working
- **Oct 18:** Public beta live

## 6. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| VideoToolbox + AVSampleBufferDisplayLayer integration takes longer than 2 weeks | High | Medium | Spike it in week 1 of M0 cleanup; if hostile, fall back to SDL2 for v0.1 and revisit |
| Noise handshake debugging eats a week | Medium | Medium | Use `snow` crate as-is; don't try to roll your own; have a fallback "TOFU + pre-shared key" path |
| Code signing / notarization paperwork blocks alpha | Medium | High | Get Developer ID enrolled in M0 week, not M7 |
| Job hunt eats more time than budgeted | High | Medium | The Aug 24 buffer week is real; alpha demo by Aug 23 means portfolio piece exists regardless |
| Windows port takes 4-5 weeks not 3 | High | Medium | M8 buffer is implicit in calendar slack; if it slips to 5 weeks, M9 starts late and M10 slides into late October |
| Self-hosted relay traffic costs blow up | Low | Low | Cap relay throughput per call; gracefully degrade quality if user is on relay |
| Sub-50ms latency target not hit | Medium | Low | The work in M1 + M6 is targeted at this. If we miss, we hit 60-70ms which is still excellent. Update marketing to "sub-100ms" if needed and move on. |

## 7. The first two weeks, day by day

This is the part that matters most because if you don't start, none of the above happens.

### Week of May 25 (M0)

- **Mon May 25:** Delete `WISHLIST.md` and `PROJECT_STRUCTURE.md`. Open the root `README.md` and rewrite it from scratch — under 200 lines, matches what the code actually does today. Commit.
- **Tue May 26:** Enroll in Apple Developer Program if not already. ($99, paperwork takes 1-2 days to clear — start the clock today). Set up `.github/workflows/ci.yml` with macos-14 runner doing `cmake --build` and `cargo build`.
- **Wed May 27:** Refactor global state — pass 1. Move `display_mutex`, `frame_queue`, `video_width/height` into `GopherClient`. Tests still pass.
- **Thu May 28:** Refactor global state — pass 2. Move the three atomics and `input_ctx`. Delete `gopher_client*` global pointer; use proper instance handle in callbacks.
- **Fri May 29:** Run a 2-hour profiling session against current code — record baseline latency CSV so you know what you're improving over. Commit baseline numbers to `docs/benchmarks/baseline-2026-05.md`.

### Week of Jun 1 (M1, half 1)

- **Mon Jun 1:** Define `PacketHeader` struct in C++. Write unit tests for serialization/deserialization with htonl/htonll round-trips. No real send/recv yet.
- **Tue Jun 2:** Rewrite `FFmpegSender::sendPacket` to send single datagrams. Update `MAX_CHUNK` to account for 24-byte header.
- **Wed Jun 3:** Rewrite `FFmpegReceiver::run` to read single datagrams. No reassembly yet — just verify the new format is read correctly with a single-fragment test.
- **Thu Jun 4:** Implement reassembly buffer. Bounded `std::map`. Eviction on age and on max-size.
- **Fri Jun 5:** Test with `dnctl pipe` simulating 1% loss. Measure metrics. Commit results to `docs/benchmarks/`.

After that, continue per the milestone plan above. Re-evaluate dates every two weeks; this calendar is the plan, not the contract.

## 8. Definition of "PM working"

- Every Friday: 15-minute self-review. What shipped this week? What slipped? Update this doc's calendar if dates moved.
- Every milestone: tag a git release (`v0.0.x`), even if nobody else uses it. Forces an integration point.
- Every alpha tester report goes into `docs/feedback/YYYY-MM-DD-name.md`. Don't lose feedback.
- Job applications: separate tracker. Don't conflate. Apply to ≥3 jobs per week regardless of gopher progress.

---

*Plan owner: ty. Next review: Fri May 29.*
