#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstring>
#include <ctime>
#include <csignal>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif



constexpr uint16_t BROADCAST_PORT = 43753;
constexpr uint16_t QUERY_PORT     = 43823;
constexpr int TIMEOUT_SECONDS     = 30;

struct Gopher {
  std::string name;
  std::string ip;
  uint16_t port;
};

std::vector<Gopher> gophers;
std::mutex gopher_mutex;


void udp_listener() { 
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  int reuse = 1;

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(BROADCAST_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(sock, (struct sockaddr*)&addr, sizeof(addr));

  char buffer[1024];
  sockaddr_in sender;
  socklen_t sender_len = sizeof(sender);

  while(true){
    int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&sender, &sender_len);
    if (n < 0) continue;
    buffer[n] = '\0'; //cap buffer

    std::string msg(buffer);
    std::string name, ip;
    uint16_t port;

    size_t n1 = msg.find("name:");
    size_t n2 = msg.find(";ip:");
    size_t n3 = msg.find(";port:");
    if (n1 != std::string::npos && n2 != std::string::npos && n3 != std::string::npos){
      name = msg.substr(n1 + 5, n2 - (n1 + 5));
      ip   = msg.substr(n2 + 4, n3 - (n2 + 4));
      port = static_cast<uint16_t>(std::stoi(msg.substr(n3 + 6)));
    }

    std::lock_guard<std::mutex> lock(gopher_mutex);
    // auto now = std::chrono::steady_clock::now();

    bool found = false;
    for (auto& g : gophers){
      if (g.name == name && g.ip == ip && g.port == port){
        found = true;
        break;
      }
    }
    if (!found) {
      gophers.push_back(Gopher{ name, ip, port });
    }
    //inexplicit unlocking of gopher mutex. gophtex
  }

}

void tcp_server() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};

  addr.sin_family = AF_INET;
  addr.sin_port   = htons(QUERY_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(sock, (sockaddr*)&addr, sizeof(addr));
  listen(sock, 5);

  while (true) {
    sockaddr_in client;
    socklen_t len = sizeof(client);

    int conn = accept(sock, (sockaddr*)&client, &len);
    if (conn < 0) continue;

    std::lock_guard<std::mutex> lock(gopher_mutex);
    std::string response;

    for (auto &g : gophers){
      response += g.name + "," + g.ip + "," + std::to_string(g.port) + "\n";
    }

    send(conn, response.c_str(), response.length(), 0);

#ifdef _WIN32
    closesocket(conn);
#else
    close(conn);
#endif
  }
}


int main()
{
  std::thread udp_thread(udp_listener);
  std::thread tcp_thread(tcp_server);

  udp_thread.join();
  tcp_thread.join();
}