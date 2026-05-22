#include "gopher/frame_reassembler.hpp"
#include "gopher/gopher_client_lib.hpp"
#include "gopher/packet.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

// Headless tests — no camera, no real network IO.
// Each test returns 0 on pass, 1 on fail.

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
    if (!client.initialize("unit-test", 0)) return 1;
    if (client.get_port() == 0)             { client.shutdown(); return 1; }
    if (client.get_name() != "unit-test")   { client.shutdown(); return 1; }
    if (client.is_in_call())                { client.shutdown(); return 1; }
    client.shutdown();
    return 0;
}

static int test_double_initialize_is_noop() {
    GopherClient client;
    if (!client.initialize("test", 0)) return 1;
    if (client.initialize("test2", 0))     { client.shutdown(); return 1; }
    client.shutdown();
    return 0;
}

// ── Wire-format helper ───────────────────────────────────────────────────────

static std::vector<uint8_t> make_datagram(uint8_t type, uint16_t flags,
                                          uint16_t stream_id,
                                          uint32_t frame_id,
                                          uint16_t frag_id, uint16_t frag_count,
                                          uint64_t capture_ts_us,
                                          const uint8_t* payload,
                                          uint16_t payload_len) {
    using namespace gopher;
    std::vector<uint8_t> buf(sizeof(PacketHeader) + payload_len);
    PacketHeader hdr{};
    hdr.version       = WIRE_VERSION;
    hdr.type          = type;
    hdr.flags         = htons(flags);
    hdr.stream_id     = htons(stream_id);
    hdr.frag_count    = htons(frag_count);
    hdr.frame_id      = htonl(frame_id);
    hdr.frag_id       = htons(frag_id);
    hdr.payload_len   = htons(payload_len);
    hdr.capture_ts_us = htonll(capture_ts_us);
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    if (payload_len) std::memcpy(buf.data() + sizeof(hdr), payload, payload_len);
    return buf;
}

// ── FrameReassembler ─────────────────────────────────────────────────────────

static int test_reasm_single_fragment_frame() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t payload[] = {0xde, 0xad, 0xbe, 0xef};
    auto dg = make_datagram(PKT_VIDEO, FLAG_KEY, 0, 1, 0, 1,
                            0x123456789ull, payload, sizeof(payload));

    if (r.ingest(dg.data(), dg.size(), 1000, out) != FrameReassembler::Result::FrameReady) return 1;
    if (out.data.size() != sizeof(payload)) return 1;
    if (std::memcmp(out.data.data(), payload, sizeof(payload)) != 0) return 1;
    if (!out.is_keyframe || out.frame_id != 1 || out.capture_ts_us != 0x123456789ull) return 1;
    if (r.frames_completed() != 1 || r.frames_dropped() != 0) return 1;
    return 0;
}

static int test_reasm_multi_fragment_in_order() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5, 6, 7};
    const uint8_t c[] = {8, 9};

    auto d0 = make_datagram(PKT_VIDEO, FLAG_KEY, 0, 42, 0, 3, 1000, a, sizeof(a));
    auto d1 = make_datagram(PKT_VIDEO, FLAG_KEY, 0, 42, 1, 3, 1000, b, sizeof(b));
    auto d2 = make_datagram(PKT_VIDEO, FLAG_KEY, 0, 42, 2, 3, 1000, c, sizeof(c));

    if (r.ingest(d0.data(), d0.size(), 1, out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d1.data(), d1.size(), 2, out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d2.data(), d2.size(), 3, out) != FrameReassembler::Result::FrameReady) return 1;

    const uint8_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    if (out.data.size() != sizeof(expected)) return 1;
    if (std::memcmp(out.data.data(), expected, sizeof(expected)) != 0) return 1;
    if (!out.is_keyframe) return 1;
    if (out.frag_received != 3 || out.frag_count != 3) return 1;
    return 0;
}

static int test_reasm_out_of_order_completes() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t a[] = {10, 20};
    const uint8_t b[] = {30, 40};
    const uint8_t c[] = {50};

    auto d0 = make_datagram(PKT_VIDEO, 0, 0, 7, 0, 3, 0, a, sizeof(a));
    auto d1 = make_datagram(PKT_VIDEO, 0, 0, 7, 1, 3, 0, b, sizeof(b));
    auto d2 = make_datagram(PKT_VIDEO, 0, 0, 7, 2, 3, 0, c, sizeof(c));

    // Deliver out of order: 2, 0, 1.
    if (r.ingest(d2.data(), d2.size(), 1, out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d0.data(), d0.size(), 2, out) != FrameReassembler::Result::Incomplete) return 1;
    if (r.ingest(d1.data(), d1.size(), 3, out) != FrameReassembler::Result::FrameReady) return 1;

    const uint8_t expected[] = {10, 20, 30, 40, 50};
    if (out.data.size() != sizeof(expected)) return 1;
    if (std::memcmp(out.data.data(), expected, sizeof(expected)) != 0) return 1;
    return 0;
}

static int test_reasm_holds_multiple_frames_in_flight() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t a[] = {1};
    const uint8_t b[] = {2};
    // Start frame 5 (2 fragments) and frame 6 (1 fragment) interleaved.
    auto frame5_part0 = make_datagram(PKT_VIDEO, 0, 0, 5, 0, 2, 0, a, 1);
    auto frame6_solo  = make_datagram(PKT_VIDEO, FLAG_KEY, 0, 6, 0, 1, 0, a, 1);
    auto frame5_part1 = make_datagram(PKT_VIDEO, 0, 0, 5, 1, 2, 0, b, 1);

    if (r.ingest(frame5_part0.data(), frame5_part0.size(), 1, out)
        != FrameReassembler::Result::Incomplete) return 1;
    // Frame 6 completes while frame 5 is still in flight.
    if (r.ingest(frame6_solo.data(), frame6_solo.size(), 2, out)
        != FrameReassembler::Result::FrameReady) return 1;
    if (out.frame_id != 6) return 1;
    if (r.pending_size() != 1) return 1;  // frame 5 still in flight

    if (r.ingest(frame5_part1.data(), frame5_part1.size(), 3, out)
        != FrameReassembler::Result::FrameReady) return 1;
    if (out.frame_id != 5 || out.data != std::vector<uint8_t>{1, 2}) return 1;
    if (r.frames_completed() != 2 || r.frames_dropped() != 0) return 1;
    return 0;
}

static int test_reasm_timeout_drops_incomplete_frame() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    const uint8_t a[] = {1};
    auto only_part = make_datagram(PKT_VIDEO, 0, 0, 99, 0, 5, 0, a, 1);

    if (r.ingest(only_part.data(), only_part.size(), 1000, out)
        != FrameReassembler::Result::Incomplete) return 1;
    if (r.pending_size() != 1) return 1;

    // Just before timeout — still pending.
    r.sweep_timeouts(1000 + FrameReassembler::TIMEOUT_US - 1);
    if (r.pending_size() != 1) return 1;
    if (r.frames_timed_out() != 0) return 1;

    // Past timeout — evicted.
    r.sweep_timeouts(1000 + FrameReassembler::TIMEOUT_US + 1);
    if (r.pending_size() != 0) return 1;
    if (r.frames_timed_out() != 1) return 1;
    return 0;
}

static int test_reasm_evicts_oldest_at_capacity() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    // Start MAX_PENDING + 1 frames, each with one fragment of a 2-fragment frame.
    const uint8_t payload[] = {0xaa};
    for (uint32_t id = 1; id <= FrameReassembler::MAX_PENDING + 1; id++) {
        auto dg = make_datagram(PKT_VIDEO, 0, 0, id, 0, 2,
                                /*capture*/ id * 100,
                                payload, 1);
        // arrival times in increasing order — first frame is oldest
        r.ingest(dg.data(), dg.size(), /*now_us=*/id * 1000, out);
    }
    if (r.pending_size() != FrameReassembler::MAX_PENDING) return 1;
    // The first frame (oldest first_arrived_us) should have been evicted.
    if (r.frames_dropped() != 1) return 1;
    return 0;
}

static int test_reasm_rejects_wrong_version() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;

    auto dg = make_datagram(PKT_VIDEO, 0, 0, 1, 0, 1, 0, nullptr, 0);
    // Corrupt the version byte after construction.
    dg[0] = 99;
    if (r.ingest(dg.data(), dg.size(), 0, out) != FrameReassembler::Result::Dropped) return 1;
    if (r.frames_dropped() != 1) return 1;
    return 0;
}

static int test_reasm_short_datagram_rejected() {
    using namespace gopher;
    FrameReassembler r;
    FrameReassembler::Output out;
    uint8_t junk[3] = {0, 0, 0};
    if (r.ingest(junk, sizeof(junk), 0, out) != FrameReassembler::Result::Dropped) return 1;
    return 0;
}

// ── runner ───────────────────────────────────────────────────────────────────

struct Test { const char* name; int (*fn)(); };
static const Test kTests[] = {
    {"initial_state",                  test_initial_state},
    {"initialize_binds_udp_port",      test_initialize_binds_udp_port},
    {"double_initialize_is_noop",      test_double_initialize_is_noop},
    {"reasm_single_fragment_frame",    test_reasm_single_fragment_frame},
    {"reasm_multi_fragment_in_order",  test_reasm_multi_fragment_in_order},
    {"reasm_out_of_order_completes",   test_reasm_out_of_order_completes},
    {"reasm_holds_multiple_in_flight", test_reasm_holds_multiple_frames_in_flight},
    {"reasm_timeout_drops_incomplete", test_reasm_timeout_drops_incomplete_frame},
    {"reasm_evicts_oldest_at_capacity",test_reasm_evicts_oldest_at_capacity},
    {"reasm_rejects_wrong_version",    test_reasm_rejects_wrong_version},
    {"reasm_short_datagram_rejected",  test_reasm_short_datagram_rejected},
};

int main() {
    int failures = 0;
    for (const auto& t : kTests) {
        const int r = t.fn();
        std::cerr << (r == 0 ? "PASS " : "FAIL ") << t.name << "\n";
        failures += r;
    }
    if (failures == 0)
        std::cout << "All " << (sizeof(kTests) / sizeof(kTests[0])) << " unit tests passed.\n";
    else
        std::cerr << failures << " unit test(s) FAILED.\n";
    return failures;
}
