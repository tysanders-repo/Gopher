#include "gopher/frame_reassembler.hpp"

#include <algorithm>
#include <cstring>

namespace gopher {

void FrameReassembler::emit(PendingFrame& pf, Output& out) {
    out.type          = pf.type;
    out.is_keyframe   = (pf.flags & FLAG_KEY) != 0;
    out.stream_id     = pf.stream_id;
    out.frame_id      = pf.frame_id;
    out.capture_ts_us = pf.capture_ts_us;
    out.frag_count    = pf.frag_count;
    out.frag_received = pf.frag_received;

    std::size_t total = 0;
    for (auto& f : pf.frags) total += f.size();
    out.data.clear();
    out.data.reserve(total);
    for (auto& f : pf.frags) {
        out.data.insert(out.data.end(), f.begin(), f.end());
    }
}

void FrameReassembler::evict_oldest() {
    if (pending_.empty()) return;
    // The "oldest" — earliest first_arrived_us, not lowest frame_id, since
    // frame_id can wrap and a late straggler may have a small id.
    auto it = std::min_element(pending_.begin(), pending_.end(),
        [](const auto& a, const auto& b) {
            return a.second.first_arrived_us < b.second.first_arrived_us;
        });
    pending_.erase(it);
    frames_dropped_++;
}

FrameReassembler::Result FrameReassembler::ingest(const uint8_t* datagram,
                                                  std::size_t len,
                                                  std::uint64_t now_us,
                                                  Output& out) {
    if (len < sizeof(PacketHeader)) {
        frames_dropped_++;
        return Result::Dropped;
    }

    PacketHeader hdr;
    std::memcpy(&hdr, datagram, sizeof(hdr));

    if (hdr.version != WIRE_VERSION) {
        frames_dropped_++;
        return Result::Dropped;
    }

    const uint16_t flags        = ntohs(hdr.flags);
    const uint16_t stream_id    = ntohs(hdr.stream_id);
    const uint16_t frag_count   = ntohs(hdr.frag_count);
    const uint32_t frame_id     = ntohl(hdr.frame_id);
    const uint16_t frag_id      = ntohs(hdr.frag_id);
    const uint16_t payload_len  = ntohs(hdr.payload_len);
    const uint64_t capture_ts   = ntohll(hdr.capture_ts_us);

    if (frag_count == 0 || frag_id >= frag_count) {
        frames_dropped_++;
        return Result::Dropped;
    }
    if (sizeof(PacketHeader) + payload_len > len) {
        frames_dropped_++;
        return Result::Dropped;
    }
    const uint8_t* payload = datagram + sizeof(PacketHeader);

    auto& pf = pending_[frame_id];
    if (pf.frags.empty()) {
        // First fragment of this frame_id we've seen.
        pf.type             = hdr.type;
        pf.flags            = flags;
        pf.stream_id        = stream_id;
        pf.frame_id         = frame_id;
        pf.frag_count       = frag_count;
        pf.frag_received    = 0;
        pf.capture_ts_us    = capture_ts;
        pf.first_arrived_us = now_us;
        pf.frags.assign(frag_count, {});
        pf.have.assign(frag_count, false);

        // Make room if we just pushed past capacity.
        while (pending_.size() > MAX_PENDING) {
            // evict_oldest may remove the entry we just inserted if it's
            // somehow already older than the rest — in practice it won't, but
            // we look up by frame_id after.
            evict_oldest();
            if (pending_.find(frame_id) == pending_.end()) {
                // Our own entry got evicted (capacity was already past).
                return Result::Dropped;
            }
        }
    } else {
        // Continuation. Validate frag_count matches.
        if (pf.frag_count != frag_count) {
            pending_.erase(frame_id);
            frames_dropped_++;
            return Result::Dropped;
        }
    }

    if (pf.have[frag_id]) {
        // Duplicate fragment — just ignore.
        return Result::Incomplete;
    }
    pf.frags[frag_id].assign(payload, payload + payload_len);
    pf.have[frag_id] = true;
    pf.frag_received++;
    // Keyframe flag may be set on any fragment of a keyframe — keep the union.
    if (flags & FLAG_KEY) pf.flags |= FLAG_KEY;

    if (pf.frag_received == pf.frag_count) {
        PendingFrame done = std::move(pf);
        pending_.erase(frame_id);
        emit(done, out);
        frames_completed_++;
        return Result::FrameReady;
    }

    return Result::Incomplete;
}

void FrameReassembler::sweep_timeouts(std::uint64_t now_us) {
    for (auto it = pending_.begin(); it != pending_.end(); ) {
        if (now_us - it->second.first_arrived_us > TIMEOUT_US) {
            it = pending_.erase(it);
            frames_timed_out_++;
        } else {
            ++it;
        }
    }
}

}  // namespace gopher
