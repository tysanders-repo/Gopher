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
#include <queue>
#include <condition_variable>

//video specific includes
#include <opencv2/opencv.hpp>

// Fix for macOS Block.h issue - include this before FFmpeg headers
#ifdef __APPLE__
#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS
#define __STDC_LIMIT_MACROS
#endif

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// my stuff :)
#include "gopherd_helper.hpp"
#include "ffmpeg_sender.hpp"
#include "ffmpeg_receiver.hpp"

#ifdef __APPLE__
#include <VideoToolbox/VideoToolbox.h>
#endif

// Declare external variables from ffmpeg_sender.cpp
extern std::queue<cv::Mat> display_queue;
extern std::mutex display_mutex;
extern std::condition_variable display_cv;

struct Gopher {
  std::string name;
  std::string ip;
  uint16_t port;
};

Gopher me_gopher;
Gopher them_gopher;

std::string gopher_name;
uint16_t listening_port;
std::vector<std::thread> threads;
std::mutex gopher_mutex;

std::queue<cv::Mat> frame_queue;
std::mutex frame_mutex;
std::condition_variable frame_cv;
pid_t gopherd_pid = -1;

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
  addr.sin_port = htons(43753);
  addr.sin_addr.s_addr = inet_addr("255.255.255.255");

  std::string message = "name:" + gopher_name + ";ip:" + get_local_ip() + ";port:" +  std::to_string(listening_port) + ";";

  while(true){
    // send the broadcast message
    // std::cout << "Broadcasting: " << message << "\n";
    sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
    sleep(5);
  }
}

std::vector<Gopher> query_daemon_for_gophers() {
  std::vector<Gopher> result;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return result;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(DAEMON_PORT);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
    char buffer[2048];
    int n = read(sock, buffer, sizeof(buffer) - 1);
    if (n > 0) {
      buffer[n] = '\0';

      std::istringstream iss(buffer);
      std::string line;
      while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string name, ip, port_str;
        if (std::getline(ls, name, ',') &&
            std::getline(ls, ip, ',') &&
            std::getline(ls, port_str)) {
          result.push_back(Gopher{name, ip, static_cast<uint16_t>(std::stoi(port_str))});
        }
      }
    }
  }
  close(sock);
  return result;
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

//---------------------------------------------

void ffmpeg_sending_thread(const std::string& ip, uint16_t port) {
    FFmpegSender sender;
    if (sender.initialize(ip, port)) {
        std::cout << "Starting FFmpeg sender to " << ip << ":" << port << std::endl;
        sender.run();
    }
}

void ffmpeg_listener_thread(int existing_sock_fd, uint16_t listen_port) {
    FFmpegReceiver receiver;
    if (receiver.initialize(existing_sock_fd, listen_port)) {
        std::cout << "Starting FFmpeg receiver on port " << listen_port << std::endl; 
        receiver.run();
    }
}

struct AVPacketData {
    std::vector<uint8_t> data;
    bool is_video;
    int64_t pts;
};

std::queue<AVPacketData> packet_queue;
std::mutex packet_mutex;
std::condition_variable packet_cv;

void setup_hardware_acceleration() {
    // For macOS - enable hardware acceleration
    #ifdef __APPLE__
    setenv("OPENCV_AVFOUNDATION_USE_NATIVE_CAPTURE", "1", 1);
    #endif
    
    // For Linux - enable V4L2 hardware acceleration
    #ifdef __linux__
    setenv("OPENCV_VIDEOIO_PRIORITY_V4L2", "1", 1);
    #endif
}

int main() {
    ensure_daemon_running("./gopherd");
    setup_hardware_acceleration();

    std::vector<std::string> menu = {"Exit"};
    int selected = 0;
    
    int listening_socket = create_listening_socket(listening_port);
    
    std::cout << "Thank you for using Gopher! Please provide a friendly name for your Gopher:\n";
    std::getline(std::cin, gopher_name);
    
    me_gopher.name = gopher_name;
    me_gopher.ip = get_local_ip();
    me_gopher.port = listening_port;
    
    std::cout << "My IP: " << me_gopher.ip << ":" << me_gopher.port << std::endl;
    
    threads.emplace_back(broadcast);
    
    while (true) {
        system("clear");
        
        auto gophers = query_daemon_for_gophers();
        
        menu.clear();
        menu.push_back("Exit");
        for (const auto& gopher : gophers) {
            // Skip self
            if (gopher.name == me_gopher.name && gopher.ip == me_gopher.ip && 
                gopher.port == me_gopher.port) continue;
            menu.push_back(gopher.name + " (" + gopher.ip + ":" + std::to_string(gopher.port) + ")");
        }
        
        for (int i = 0; i < menu.size(); i++) {
            if (i == selected)
                std::cout << "> " << menu[i] << "\n";
            else
                std::cout << "  " << menu[i] << "\n";
        }
        
        char c = getch();
        if (c == 27) {
            getch();
            char arrow = getch();
            if (arrow == 'A') selected = (selected - 1 + menu.size()) % menu.size();
            if (arrow == 'B') selected = (selected + 1) % menu.size();
        } else if (c == '\n') {
            if (menu[selected] == "Exit") break;
            
            // Find selected gopher
            Gopher selected_gopher;
            bool found = false;
            for (const auto& gopher : gophers) {
                if (gopher.name == me_gopher.name && gopher.ip == me_gopher.ip && 
                    gopher.port == me_gopher.port) continue;
                
                std::string gopher_display = gopher.name + " (" + gopher.ip + ":" + std::to_string(gopher.port) + ")";
                if (menu[selected] == gopher_display) {
                    selected_gopher = gopher;
                    found = true;
                    break;
                }
            }
            
            if (found) {
                std::cout << "Connecting to " << selected_gopher.name << "..." << std::endl;
                threads.emplace_back(ffmpeg_sending_thread, selected_gopher.ip, selected_gopher.port);
                threads.emplace_back(ffmpeg_listener_thread, listening_socket, listening_port);
                    
                // Display received video
                cv::namedWindow("Received Video", cv::WINDOW_AUTOSIZE);
                    
                while (true) {
                    std::unique_lock<std::mutex> lock(display_mutex);
                    display_cv.wait(lock, [] { return !display_queue.empty(); });
                    
                    cv::Mat frame = display_queue.front();
                    display_queue.pop();
                    lock.unlock();
                    
                    cv::imshow("Received Video", frame);
                    if (cv::waitKey(1) == 27) break; // ESC to exit
                }
                
                cv::destroyAllWindows();
                std::cout << "Stopped receiving video." << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}