#include "gopher/frame_reassembler.hpp"

#include <cstring>

namespace gopher {

void FrameReassembler::reset_inflight() {
    have_inflight_ = false;
    inflight_seq_ = 0;
    next_fragment_idx_ = 0;
    inflight_data_.clear();
}

FrameReassembler::Result FrameReassembler::ingest(const uint8_t* datagram,
                                                  std::size_t len,
                                                  Output& out) {
    if (len < sizeof(PacketHeader)) {
        if (have_inflight_) { frames_dropped_++; reset_inflight(); }
        return Result::Dropped;
    }

    PacketHeader hdr;
    std::memcpy(&hdr, datagram, sizeof(hdr));

    const uint32_t seq          = ntohl(hdr.frame_seq);
    const uint16_t idx          = ntohs(hdr.fragment_idx);
    const uint64_t ts_us        = ntohll(hdr.timestamp_us);
    const bool     is_start     = hdr.flags & FLAG_START;
    const bool     is_end       = hdr.flags & FLAG_END;
    const bool     is_key       = hdr.flags & FLAG_KEY;
    const uint8_t* payload      = datagram + sizeof(PacketHeader);
    const std::size_t pay_len   = len - sizeof(PacketHeader);

    auto finalize = [&]() {
        out.data         = std::move(inflight_data_);
        out.type         = inflight_type_;
        out.is_keyframe  = inflight_is_key_;
        out.timestamp_us = inflight_timestamp_;
        out.frame_seq    = inflight_seq_;
        frames_completed_++;
        reset_inflight();
        return Result::FrameReady;
    };

    if (is_start) {
        // New frame begins. If we had something in flight, that frame is lost.
        if (have_inflight_) { frames_dropped_++; }
        have_inflight_       = true;
        inflight_seq_        = seq;
        inflight_type_       = hdr.type;
        inflight_timestamp_  = ts_us;
        inflight_is_key_     = is_key;
        inflight_data_.assign(payload, payload + pay_len);
        next_fragment_idx_   = 1;

        if (is_end) return finalize();
        return Result::Incomplete;
    }

    // Continuation fragment.
    if (!have_inflight_ ||
        seq != inflight_seq_ ||
        idx != next_fragment_idx_) {
        // Out-of-order, late, or stray fragment — drop any in-flight frame.
        if (have_inflight_) { frames_dropped_++; reset_inflight(); }
        return Result::Dropped;
    }

    inflight_data_.insert(inflight_data_.end(), payload, payload + pay_len);
    next_fragment_idx_++;

    if (is_end) return finalize();
    return Result::Incomplete;
}

}  // namespace gopher
