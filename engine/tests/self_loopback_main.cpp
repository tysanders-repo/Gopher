// Local UDP loopback: capture from camera, encode, send to 127.0.0.1:listen_port,
// receive on the same bound socket, decode, display via SDL (main thread).
#include "gopher/gopher_client_lib.hpp"

#include <iostream>

int main() {
  GopherClient client;
  client.enable_dev_mode(true);

  if (!client.initialize("self-loopback", 0)) {
    std::cerr << "initialize(name, recv_port=0) failed\n";
    return 1;
  }

  const uint16_t port = client.get_port();
  std::cout << "Listening on UDP port " << port
            << " — starting sender to 127.0.0.1 (same port)\n";

  if (!client.start_call("127.0.0.1", port)) {
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
