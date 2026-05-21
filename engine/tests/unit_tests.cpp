#include "gopher/frame_reassembler.hpp"
#include "gopher/gopher_client_lib.hpp"
#include "gopher/packet.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

// Returns 0 on pass, 1 on fail. Headless tests — no camera, no network IO.

// ── GopherClient state ───────────────────────────────────────────────────────

static int test_initial_state() {
    GopherClient client;
    assert(!client.is_in_call());
    assert(client.get_name().empty());
    assert(client.get_port() == 0);
    return 0;
}

static int test_initialize_binds_udp_port() {
    GopherClient client;
    if (!client.initialize("unit-test", 0)) {
        std::cerr << "FAIL: initialize returned false\n";
        return 1;
    }
    if (client.get_port() == 0) {
        std::cerr << "FAIL: expected non-zero port after initialize\n";
        client.shutdown();
        return 1;
    }
    if (client.get_name() != "unit-test") {
        std::cerr << "FAIL: name mismatch\n";
        client.shutdown();
        return 1;
    }
    if (client.is_in_call()) {
        std::cerr << "FAIL: should not be in call after initialize\n";
        client.shutdown();
        return 1;
    }
    client.shutdown();
    return 0;
}

static int test_double_initialize_is_noop() {
    GopherClient client;
    if (!client.initialize("test", 0)) return 1;
    if (client.initialize("test2", 0)) {
        std::cerr << "FAIL: double initialize should return false\n";
        client.shutdown();
        return 1;
    }
    client.shutdown();
    return 0;
}

// ── FrameReassembler ─────────────────────────────────────────────────────────
// Build a wire-format datagram by hand and feed it to the reassembler.

static std::vector<uint8_t> make_datagram(uint8_t type, uint8_t flags,
                                          uint16_t frag_idx, uint32_t frame_seq,
                                          uint64_t ts_us,
                                          const uint8_t* payload, std::size_t len) {
    using namespace gopher;
    std::vector<uint8_t> buf(sizeof(PacketHeader) + len);
    PacketHeader hdr{};
    hdr.type         = type;
    hdr.flags        = flags;
    hdr.fragment_idx = htons(frag_idx);
    hdr.frame_seq    = htonl(frame_seq);
    hdr.timestamp_us = htonll(ts_us);
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    if (len) std::memcpy(buf.data() + sizeof(hdr), payload, len);
    return buf;
}

static int test_reasm_single_fragment_frame() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t payload[] = {0xde, 0xad, 0xbe, 0xef};
    auto dg = make_datagram(PKT_VIDEO, FLAG_START | FLAG_END | FLAG_KEY,
                            0, 1, 0x123456789ull, payload, sizeof(payload));

    if (r.ingest(dg.data(), dg.size(), out) != FrameReassembler::Result::FrameReady) {
        std::cerr << "FAIL: single-fragment frame did not assemble\n";
        return 1;
    }
    if (out.data.size() != sizeof(payload) ||
        std::memcmp(out.data.data(), payload, sizeof(payload)) != 0) {
        std::cerr << "FAIL: payload mismatch\n";
        return 1;
    }
    if (!out.is_keyframe || out.timestamp_us != 0x123456789ull || out.frame_seq != 1) {
        std::cerr << "FAIL: header fields lost in reassembly\n";
        return 1;
    }
    if (r.frames_completed() != 1 || r.frames_dropped() != 0) return 1;
    return 0;
}

static int test_reasm_multi_fragment_frame() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5, 6, 7};
    const uint8_t c[] = {8, 9};

    auto d0 = make_datagram(PKT_VIDEO, FLAG_START | FLAG_KEY, 0, 42, 1000, a, sizeof(a));
    auto d1 = make_datagram(PKT_VIDEO, 0,                     1, 42, 1000, b, sizeof(b));
    auto d2 = make_datagram(PKT_VIDEO, FLAG_END,              2, 42, 1000, c, sizeof(c));

    if (r.ingest(d0.data(), d0.size(), out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d1.data(), d1.size(), out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d2.data(), d2.size(), out) != FrameReassembler::Result::FrameReady) {
        std::cerr << "FAIL: 3-fragment frame did not complete\n";
        return 1;
    }
    const uint8_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    if (out.data.size() != sizeof(expected) ||
        std::memcmp(out.data.data(), expected, sizeof(expected)) != 0) {
        std::cerr << "FAIL: reassembled payload incorrect\n";
        return 1;
    }
    if (!out.is_keyframe) {
        std::cerr << "FAIL: keyframe flag lost\n";
        return 1;
    }
    return 0;
}

static int test_reasm_drops_on_missing_middle_fragment() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t a[] = {1, 2};
    const uint8_t c[] = {5, 6};

    auto d0 = make_datagram(PKT_VIDEO, FLAG_START, 0, 7, 0, a, sizeof(a));
    // (datagram with frag_idx=1 is "lost")
    auto d2 = make_datagram(PKT_VIDEO, FLAG_END,   2, 7, 0, c, sizeof(c));

    if (r.ingest(d0.data(), d0.size(), out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d2.data(), d2.size(), out) != FrameReassembler::Result::Dropped) {
        std::cerr << "FAIL: skipped middle fragment should be dropped\n";
        return 1;
    }
    if (r.frames_dropped() != 1) {
        std::cerr << "FAIL: expected drop counter to increment\n";
        return 1;
    }
    return 0;
}

static int test_reasm_new_start_discards_inflight() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t a[] = {1, 2};
    const uint8_t b[] = {9};

    // Start frame 5, never finish.
    auto d0 = make_datagram(PKT_VIDEO, FLAG_START, 0, 5, 0, a, sizeof(a));
    // Then frame 6 starts AND ends in one datagram.
    auto d1 = make_datagram(PKT_VIDEO, FLAG_START | FLAG_END, 0, 6, 0, b, sizeof(b));

    if (r.ingest(d0.data(), d0.size(), out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d1.data(), d1.size(), out) != FrameReassembler::Result::FrameReady) {
        std::cerr << "FAIL: new START should complete a single-datagram frame\n";
        return 1;
    }
    if (out.frame_seq != 6 || out.data.size() != 1 || out.data[0] != 9) {
        std::cerr << "FAIL: new frame contents wrong\n";
        return 1;
    }
    if (r.frames_dropped() != 1 || r.frames_completed() != 1) {
        std::cerr << "FAIL: drop/complete counters wrong\n";
        return 1;
    }
    return 0;
}

static int test_reasm_short_datagram_rejected() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;
    uint8_t junk[3] = {0, 0, 0};
    if (r.ingest(junk, sizeof(junk), out) != FrameReassembler::Result::Dropped) {
        std::cerr << "FAIL: sub-header datagram should be dropped\n";
        return 1;
    }
    return 0;
}

int main() {
    int failures = 0;
    failures += test_initial_state();
    failures += test_initialize_binds_udp_port();
    failures += test_double_initialize_is_noop();
    failures += test_reasm_single_fragment_frame();
    failures += test_reasm_multi_fragment_frame();
    failures += test_reasm_drops_on_missing_middle_fragment();
    failures += test_reasm_new_start_discards_inflight();
    failures += test_reasm_short_datagram_rejected();

    if (failures == 0)
        std::cout << "All unit tests passed.\n";
    else
        std::cerr << failures << " unit test(s) FAILED.\n";

    return failures;
}
