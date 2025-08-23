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
#include <algorithm>
#include <poll.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <errno.h>
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

// Fixed daemon cleanup
volatile sig_atomic_t running = 1;

void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
    running = 0;
}

int udp_listener_safe() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
      std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
      return -1;
    }
    
    // Set socket timeout to allow checking running flag
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cerr << "Failed to set socket timeout: " << strerror(errno) << std::endl;
    }
    
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BROADCAST_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind UDP socket to port " << BROADCAST_PORT 
                  << ": " << strerror(errno) << std::endl;
        close(sock);
        return -1;
    }
    
    std::cout << "UDP listener started on port " << BROADCAST_PORT << std::endl;
    
    char buffer[1024];
    sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);

    struct pollfd pfd{.fd = sock, .events = POLLIN};
    
    while (running) {
        int ret = poll(&pfd, 1, 1000);
        if (ret < 0) {
            std::cerr << "Poll error: " << strerror(errno) << std::endl;
            break;
        } else if (ret == 0) {
            continue;
        }

        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, 
                       (struct sockaddr*)&sender, &sender_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // Timeout, check running flag
            }
            std::cerr << "UDP recvfrom error: " << strerror(errno) << std::endl;
            break;
        }
        
        buffer[n] = '\0';
        
        // Parse message safely
        std::string msg(buffer);
        size_t name_pos = msg.find("name:");
        size_t ip_pos = msg.find(";ip:");
        size_t port_pos = msg.find(";port:");
        
        if (name_pos == std::string::npos || ip_pos == std::string::npos || 
            port_pos == std::string::npos) {
            std::cout << "Invalid message format: " << msg << std::endl;
            continue;
        }
        
        std::string name = msg.substr(name_pos + 5, ip_pos - (name_pos + 5));
        std::string ip = msg.substr(ip_pos + 4, port_pos - (ip_pos + 4));
        
        try {
            uint16_t port = static_cast<uint16_t>(std::stoi(msg.substr(port_pos + 6)));
            
            std::lock_guard<std::mutex> lock(gopher_mutex);
            
            // Remove duplicates and add new gopher
            gophers.erase(std::remove_if(gophers.begin(), gophers.end(),
                [&](const Gopher& g) {
                    return g.name == name && g.ip == ip && g.port == port;
                }), gophers.end());
            
            gophers.push_back(Gopher{name, ip, port});
            std::cout << "Added/updated gopher: " << name << "@" << ip << ":" << port << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Invalid port number in message: " << msg << std::endl;
            continue;
        }
    }
    
    std::cout << "UDP listener shutting down" << std::endl;
    close(sock);
    
    
#ifdef _WIN32
    WSACleanup();
#endif

  return 0;
}

void tcp_server_safe() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create TCP socket: " << strerror(errno) << std::endl;
        return;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cerr << "Failed to set TCP socket timeout: " << strerror(errno) << std::endl;
    }
    
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR on TCP socket: " << strerror(errno) << std::endl;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(QUERY_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind TCP socket to port " << QUERY_PORT 
                  << ": " << strerror(errno) << std::endl;
        close(sock);
        return;
    }
    
    if (listen(sock, 5) < 0) {
        std::cerr << "Failed to listen on TCP socket: " << strerror(errno) << std::endl;
        close(sock);
        return;
    }
    
    std::cout << "TCP server started on port " << QUERY_PORT << std::endl;
    
    while (running) {
        sockaddr_in client;
        socklen_t len = sizeof(client);
        
        int conn = accept(sock, (sockaddr*)&client, &len);
        if (conn < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            std::cerr << "TCP accept error: " << strerror(errno) << std::endl;
            break;
        }
        
        std::lock_guard<std::mutex> lock(gopher_mutex);
        std::string response;
        
        for (const auto& g : gophers) {
            response += g.name + "," + g.ip + "," + std::to_string(g.port) + "\n";
        }
        
        if (send(conn, response.c_str(), response.length(), 0) < 0) {
            std::cerr << "Failed to send response: " << strerror(errno) << std::endl;
        }
        
        close(conn);
    }
    
    std::cout << "TCP server shutting down" << std::endl;
    close(sock);
}

int main(int argc, char* argv[]) {
    std::cout << "Starting gopherd..." << std::endl;
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Start the server threads
        std::thread udp_thread(udp_listener_safe);
        std::thread tcp_thread(tcp_server_safe);
        
        // Clean shutdown
        if (udp_thread.joinable()) {
            udp_thread.join();
        }
        if (tcp_thread.joinable()) {
            tcp_thread.join();
        }
        
        std::cout << "Gopherd stopped." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception in main" << std::endl;
        return 1;
    }
    
    return 0;
}