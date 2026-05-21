// Two-process bidirectional test: each instance binds its UDP listen port and sends
// encoded video to the peer's listen port. Run twice in two terminals, swapping ports.
//
// Terminal A: ./gopher_dual_peer_test 50100 50101
// Terminal B: ./gopher_dual_peer_test 50101 50100
//
// On macOS, two processes opening the default camera may conflict; use a second device
// for one side, e.g. GOPHER_AVFOUNDATION_DEVICE=1: ./gopher_dual_peer_test 50101 50100
#include "gopher/gopher_client_lib.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " <my_listen_port> <peer_listen_port> [peer_ip]\n"
      << "\n"
      << "Run two instances with swapped ports (same peer_ip, default 127.0.0.1).\n"
      << "  Terminal A: " << argv0 << " 50100 50101\n"
      << "  Terminal B: " << argv0 << " 50101 50100\n"
      << "\n"
      << "Optional env (macOS avfoundation device string):\n"
      << "  GOPHER_AVFOUNDATION_DEVICE=1:   # second instance if default camera is busy\n";
}

static bool parse_u16(const char* s, uint16_t& out) {
  char* end = nullptr;
  unsigned long v = std::strtoul(s, &end, 10);
  if (!end || *end != '\0' || v > 65535) return false;
  out = static_cast<uint16_t>(v);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    usage(argv[0]);
    return 1;
  }

  uint16_t my_port = 0;
  uint16_t peer_port = 0;
  if (!parse_u16(argv[1], my_port) || !parse_u16(argv[2], peer_port)) {
    std::cerr << "Invalid port (expect 0-65535).\n";
    usage(argv[0]);
    return 1;
  }

  if (my_port == peer_port) {
    std::cerr << "my_listen_port and peer_listen_port must differ.\n";
    return 1;
  }

  const std::string peer_ip = (argc >= 4) ? argv[3] : "127.0.0.1";

  GopherClient client;
  client.enable_dev_mode(true);

  const std::string name = "peer-" + std::to_string(my_port);
  if (!client.initialize(name, my_port)) {
    std::cerr << "initialize(\"" << name << "\", " << my_port << ") failed\n";
    return 1;
  }

  if (client.get_port() != my_port) {
    std::cerr << "Bound port mismatch (expected " << my_port << ", got " << client.get_port()
              << ").\n";
    client.shutdown();
    return 1;
  }

  std::cout << "Listening on UDP " << my_port << " — sending to " << peer_ip << ":" << peer_port
            << "\n";

  if (!client.start_call(peer_ip, peer_port)) {
    std::cerr << "start_call failed\n";
    client.shutdown();
    return 1;
  }

  std::cout << "Quit the SDL window (or Ctrl+C) to exit.\n";
  client.process_video_display();

  client.end_call();
  client.shutdown();
  return 0;
}
