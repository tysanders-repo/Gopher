#ifndef GOPHER_FRAME_REASSEMBLER_HPP
#define GOPHER_FRAME_REASSEMBLER_HPP

#include "gopher/packet.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace gopher {

// Stateful per-receiver reassembler. Feed every received datagram in via
// `ingest(...)`. When a frame completes, the assembled payload is returned in
// `out` and `ingest` returns true. On any out-of-order, missing-fragment, or
// frame-boundary anomaly the in-flight frame is discarded.
//
// Counters track lost frames for backpressure / diagnostics.
class FrameReassembler {
public:
    enum class Result {
        Incomplete,   // fragment accepted, more expected
        FrameReady,   // out contains a complete frame; type/keyframe/timestamp are valid
        Dropped,      // garbage or out-of-order; any in-flight frame was discarded
    };

    struct Output {
        std::vector<uint8_t> data;
        uint8_t  type;
        bool     is_keyframe;
        uint64_t timestamp_us;
        uint32_t frame_seq;
    };

    Result ingest(const uint8_t* datagram, std::size_t len, Output& out);

    std::uint64_t frames_completed() const { return frames_completed_; }
    std::uint64_t frames_dropped()   const { return frames_dropped_; }

private:
    void reset_inflight();

    bool     have_inflight_{false};
    uint32_t inflight_seq_{0};
    uint16_t next_fragment_idx_{0};
    uint8_t  inflight_type_{0};
    uint64_t inflight_timestamp_{0};
    bool     inflight_is_key_{false};
    std::vector<uint8_t> inflight_data_;

    std::uint64_t frames_completed_{0};
    std::uint64_t frames_dropped_{0};
};

}  // namespace gopher

#endif  // GOPHER_FRAME_REASSEMBLER_HPP
