#ifndef GOPHER_PACKET_HPP
#define GOPHER_PACKET_HPP

// Wire format v1: one fragment per UDP datagram.
//
// Layout (24-byte header, all multi-byte fields network byte order):
//
//   off  size  field          notes
//   ---  ----  -------------  ---------------------------------------------
//    0    1    version        WIRE_VERSION (currently 1)
//    1    1    type           PKT_VIDEO / PKT_AUDIO / PKT_CONFIG
//    2    2    flags          FLAG_KEY etc.
//    4    2    stream_id      0 = default video; reserved for mux
//    6    2    frag_count     total fragments in this frame
//    8    4    frame_id       monotonic per (stream_id, sender)
//   12    2    frag_id        0-based fragment index within frame
//   14    2    payload_len    payload bytes in THIS datagram
//   16    8    capture_ts_us  sender's steady_clock micros at capture time
//   24    N    payload        N <= MAX_PAYLOAD

#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>

namespace gopher {

constexpr uint8_t WIRE_VERSION = 1;

constexpr uint8_t PKT_VIDEO  = 1;
constexpr uint8_t PKT_AUDIO  = 2;
constexpr uint8_t PKT_CONFIG = 3;

constexpr uint16_t FLAG_KEY = 0x0001;  // first fragment of a keyframe

constexpr std::size_t MAX_DATAGRAM = 1400;

struct PacketHeader {
    uint8_t  version;
    uint8_t  type;
    uint16_t flags;
    uint16_t stream_id;
    uint16_t frag_count;
    uint32_t frame_id;
    uint16_t frag_id;
    uint16_t payload_len;
    uint64_t capture_ts_us;
};
static_assert(sizeof(PacketHeader) == 24, "PacketHeader must be exactly 24 bytes");

constexpr std::size_t MAX_PAYLOAD = MAX_DATAGRAM - sizeof(PacketHeader);

}  // namespace gopher

#endif  // GOPHER_PACKET_HPP
