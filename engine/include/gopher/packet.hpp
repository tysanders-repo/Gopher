#ifndef GOPHER_PACKET_HPP
#define GOPHER_PACKET_HPP

// Wire format for Gopher media packets.
//
// Each UDP datagram carries one fragment of one frame: a fixed 16-byte header
// followed by 0..GOPHER_MAX_PAYLOAD bytes of payload. Fragments of the same
// frame share frame_seq and have increasing fragment_idx; the first carries
// the START flag, the last carries the END flag (a single-fragment frame has
// both). The receiver assembles consecutive in-order fragments and drops the
// in-flight frame on any gap, reorder, or new START arriving mid-frame.
//
// All multi-byte fields are network byte order on the wire.

#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>

namespace gopher {

constexpr uint8_t PKT_VIDEO  = 1;
constexpr uint8_t PKT_AUDIO  = 2;
constexpr uint8_t PKT_CONFIG = 3;  // reserved for SPS/PPS push (Phase B+)

constexpr uint8_t FLAG_START = 0x01;
constexpr uint8_t FLAG_END   = 0x02;
constexpr uint8_t FLAG_KEY   = 0x04;  // first fragment of a keyframe

// Conservative: 1500 (typical Ethernet MTU) − 20 (IPv4) − 8 (UDP) − header.
constexpr std::size_t MAX_DATAGRAM = 1400;

struct PacketHeader {
    uint8_t  type;          // PKT_*
    uint8_t  flags;         // FLAG_*
    uint16_t fragment_idx;  // 0-based within frame, network order
    uint32_t frame_seq;     // sender's monotonic counter, network order
    uint64_t timestamp_us;  // send time (microseconds since epoch), network order
};
static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be 16 bytes");

constexpr std::size_t MAX_PAYLOAD = MAX_DATAGRAM - sizeof(PacketHeader);

}  // namespace gopher

#endif  // GOPHER_PACKET_HPP
