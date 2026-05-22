#ifndef GOPHER_FRAME_REASSEMBLER_HPP
#define GOPHER_FRAME_REASSEMBLER_HPP

#include "gopher/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace gopher {

// Per-receiver UDP fragment reassembler.
//
// Holds up to MAX_PENDING in-flight frames in a map keyed by frame_id.
// When the table overflows, the oldest frame is evicted. Frames that haven't
// completed within TIMEOUT_US are dropped by sweep_timeouts().
//
// The receiver feeds each datagram into ingest(); when one completes the
// frame, ingest returns FrameReady and out is populated with the assembled
// payload + metadata. Periodically (or after each ingest) the caller should
// also call sweep_timeouts() with the current monotonic clock to age out
// frames whose fragments never finished arriving.
class FrameReassembler {
public:
    static constexpr std::size_t MAX_PENDING = 8;
    static constexpr std::uint64_t TIMEOUT_US = 50'000;  // 50 ms

    enum class Result {
        Incomplete,   // fragment accepted, more expected
        FrameReady,   // out is valid
        Dropped,      // datagram rejected (bad header, wrong version, etc.)
    };

    struct Output {
        std::vector<uint8_t> data;
        uint8_t  type;
        bool     is_keyframe;
        uint16_t stream_id;
        uint32_t frame_id;
        uint64_t capture_ts_us;
        uint16_t frag_count;
        uint16_t frag_received;  // == frag_count on FrameReady
    };

    // now_us is the receiver's monotonic clock in microseconds.
    Result ingest(const uint8_t* datagram, std::size_t len,
                  std::uint64_t now_us, Output& out);

    // Evict frames whose first fragment arrived more than TIMEOUT_US ago.
    // Increments frames_timed_out_ for each evicted frame.
    void sweep_timeouts(std::uint64_t now_us);

    std::uint64_t frames_completed()  const { return frames_completed_; }
    std::uint64_t frames_dropped()    const { return frames_dropped_; }
    std::uint64_t frames_timed_out()  const { return frames_timed_out_; }
    std::size_t   pending_size()      const { return pending_.size(); }

private:
    struct PendingFrame {
        uint8_t  type{0};
        uint16_t flags{0};
        uint16_t stream_id{0};
        uint32_t frame_id{0};
        uint16_t frag_count{0};
        uint16_t frag_received{0};
        uint64_t capture_ts_us{0};
        uint64_t first_arrived_us{0};
        // Fragment storage. We assemble lazily on completion to avoid the
        // inner vector-of-vector noise; instead we pre-size to frag_count and
        // record (offset, length) for each fragment, then concatenate on done.
        std::vector<std::vector<uint8_t>> frags;  // size == frag_count
        std::vector<bool> have;                   // size == frag_count
    };

    void emit(PendingFrame& pf, Output& out);
    void evict_oldest();

    std::map<uint32_t, PendingFrame> pending_;
    std::uint64_t frames_completed_{0};
    std::uint64_t frames_dropped_{0};
    std::uint64_t frames_timed_out_{0};
};

}  // namespace gopher

#endif  // GOPHER_FRAME_REASSEMBLER_HPP
