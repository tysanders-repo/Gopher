#include <iostream>
#include <vector>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <mutex>
#include <fcntl.h>
#include <chrono>

#include "gopherd_helper.hpp"


struct Gopher {
  std::string name;
  std::string ip;
  uint16_t port;
};

Gopher me_gopher;


std::string gopher_name;
uint16_t listening_port;
std::vector<std::thread> threads;
std::vector<Gopher> gophers;
std::mutex gopher_mutex;



/* 
  defintely not my original code, common pattern to get local IP address
*/
std::string get_local_ip() {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  std::string ip = "127.0.0.1";

  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(80);

  inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

  if (connect(sock, (struct sockaddr*)&remote, sizeof(remote)) == 0) {
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (getsockname(sock, (struct sockaddr*)&local, &len) == 0) {
      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &local.sin_addr, ip_str, sizeof(ip_str));
      ip = std::string(ip_str);
    }
  }

  close(sock);
  return std::string(ip);
}

char getch() {
  termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO); // disable buffering and echo
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  char ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  return ch;
}

int broadcast(){
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  int broadcast_enable = 1;

  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

  sockaddr_in addr{}; 
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8888);
  addr.sin_addr.s_addr = inet_addr("255.255.255.255");


  std::string message = "{name:" + gopher_name + ";ip:" + get_local_ip() + ";port:" +  std::to_string(listening_port) + ";}";

  while(true){

    // send the broadcast message
    // std::cout << "Broadcasting: " << message << "\n";
    sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
    sleep(5);
  }
  
}

int listen_for_broadcasts(){
  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8888);
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(sock, (struct sockaddr*)&addr, sizeof(addr));
  char buffer[1024];

  while(true) {
    int len = sizeof(addr);
    int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&addr, (socklen_t*)&len);
    buffer[n] = '\0';

    // Parse the received message
    std::string message(buffer);
    size_t name_start = message.find("name:") + 5;
    size_t name_end = message.find(";", name_start);
    size_t ip_start = message.find("ip:") + 3;
    size_t ip_end = message.find(";", ip_start);
    size_t port_start = message.find("port:") + 5;
    size_t port_end = message.find(";", port_start);
    std::string name = message.substr(name_start, name_end - name_start);
    std::string ip = message.substr(ip_start, ip_end - ip_start);
    uint16_t port = std::stoi(message.substr(port_start, port_end - port_start));

    // Add to gophers list
    Gopher gopher;
    gopher.name = name;
    gopher.ip = ip;
    gopher.port = port;

    // print out received gopher info
    // std::cout << "Received Gopher: " << gopher.name << " (" << gopher.ip << ":" << gopher.port << ")\n";s

    // ensure we don't add duplicates
    gopher_mutex.lock();
    bool is_me = (
      gopher.name == me_gopher.name &&
      gopher.ip == me_gopher.ip &&
      gopher.port == me_gopher.port
    );

    if (!is_me) {
      bool exists = false;
      for (const auto& existing : gophers) {
        if (
          existing.name == gopher.name &&
          existing.ip == gopher.ip &&
          existing.port == gopher.port
        ) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        gophers.push_back(gopher);
      }
    }
    gopher_mutex.unlock();
  }
}


int create_listening_socket(uint16_t& out_port) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0); //let OS choose
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(sock, (struct sockaddr*)&addr, sizeof(addr));

  socklen_t len = sizeof(addr);
  getsockname(sock, (struct sockaddr*)&addr, &len);
  out_port = ntohs(addr.sin_port);

  return sock;
}



int main() {
  std::vector<std::string> menu = {"Exit"};
  int selected = 0;

  /*
    Initialize the listening socket and bind so we know the port for broadcasting
  */

  int listening_socket = create_listening_socket(listening_port);


  //TODO: save preferences to a file
  std::cout << "Thank you for using Gopher! Please provide a friendly name for your Gopher:\n";
  std::getline(std::cin, gopher_name);
  std::string my_ip = get_local_ip();

  std::cout << "ip addr: " << my_ip << "\n";

  me_gopher.name = gopher_name;
  me_gopher.ip =  my_ip;
  me_gopher.port = listening_port;

  /*
    Start the broadcast thread to announce our presence on the network
    This will run in the background and send our name, IP, and port every 5 seconds
    we'll assign a thread to it so it can run concurrently
  */

  threads.emplace_back(broadcast);
  threads.emplace_back(listen_for_broadcasts);

  while (true) {
    system("clear");

    // Update the menu with discovered gophers
    gopher_mutex.lock();
    menu.clear();
    menu.push_back("Exit");
    for (const auto& gopher : gophers) {
      menu.push_back(gopher.name + " (" + gopher.ip + ":" + std::to_string(gopher.port) + ")");
    }
    gopher_mutex.unlock();

    
    for (int i = 0; i < menu.size(); i++) {
      if (i == selected)
          std::cout << "> " << menu[i] << "\n";
      else
          std::cout << "  " << menu[i] << "\n";
    }

    if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) == -1) {
    perror("fcntl");
    }

    char c = getch();
    if (c == 27) { // Arrow key prefix
      getch();    // skip '['
      char arrow = getch();
      if (arrow == 'A') selected = (selected - 1 + menu.size()) % menu.size(); // Up
      if (arrow == 'B') selected = (selected + 1) % menu.size();               // Down
    } else if (c == '\n') {
      std::cout << "You selected: " << menu[selected] << "\n";
      if (menu[selected] == "Exit") break;
      sleep(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
