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

//video specific includes
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// my stuff :)
#include "gopherd_helper.hpp"

#ifdef __APPLE__
#include <VideoToolbox/VideoToolbox.h>
#endif


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




void sending_thread(const std::string& ip, uint16_t port) {
    std::cout << "Starting sender to " << ip << ":" << port << std::endl;
    
    // Simple OpenCV capture
    cv::VideoCapture cap(0, cv::CAP_AVFOUNDATION);
    if (!cap.isOpened()) {
        std::cerr << "Cannot open camera!" << std::endl;
        return;
    }
    
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
    cap.set(cv::CAP_PROP_FPS, 60);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G')); // Hardware MJPEG
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    
    cv::Mat frame;
    std::vector<uchar> buffer;
    std::vector<int> params = {
        cv::IMWRITE_JPEG_QUALITY, 25,           // Good quality/speed balance
        cv::IMWRITE_JPEG_OPTIMIZE, 1,           // Optimize Huffman tables
    };
    
    std::cout << "Sending video..." << std::endl;
    
    while (true) {
        cap >> frame;
        if (frame.empty()) {
            std::cerr << "Empty frame" << std::endl;
            continue;
        }
        
        // Encode as JPEG
        if (!cv::imencode(".jpg", frame, buffer, params)) {
            std::cerr << "Failed to encode frame" << std::endl;
            continue;
        }
        
        // Send size first, then data
        uint32_t size = htonl(buffer.size());
        sendto(sock, &size, sizeof(size), 0, (sockaddr*)&addr, sizeof(addr));
        
        // Send in chunks if needed
        const size_t MAX_CHUNK = 1400;
        size_t offset = 0;
        while (offset < buffer.size()) {
            size_t chunk_size = std::min(MAX_CHUNK, buffer.size() - offset);
            sendto(sock, buffer.data() + offset, chunk_size, 0, (sockaddr*)&addr, sizeof(addr));
            offset += chunk_size;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10 FPS
    }
    
    close(sock);
    cap.release();
}

void listener_thread(int sock) {
    std::cout << "Starting listener on socket " << sock << std::endl;
    
    std::vector<uchar> buffer;
    uchar recv_buf[2048];
    
    while (true) {
        // Receive size
        uint32_t expected_size;
        int n = recvfrom(sock, &expected_size, sizeof(expected_size), 0, nullptr, nullptr);
        if (n != sizeof(expected_size)) continue;
        
        expected_size = ntohl(expected_size);
        if (expected_size > 100000) continue; // Sanity check
        
        buffer.clear();
        buffer.reserve(expected_size);
        
        // Receive data
        while (buffer.size() < expected_size) {
            n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, nullptr, nullptr);
            if (n <= 0) break;
            
            buffer.insert(buffer.end(), recv_buf, recv_buf + n);
        }
        
        if (buffer.size() >= expected_size) {
            // Decode JPEG and add to queue for main thread
            cv::Mat img = cv::imdecode(buffer, cv::IMREAD_COLOR);
            if (!img.empty()) {
                std::lock_guard<std::mutex> lock(frame_mutex);
                frame_queue.push(img.clone());
                frame_cv.notify_one();
            }
        }
    }
    
    close(sock);
}


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
    threads.emplace_back(listener_thread, listening_socket);
    
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
                threads.emplace_back(sending_thread, selected_gopher.ip, selected_gopher.port);
                
                // Display received video on main thread (macOS requirement)
                cv::namedWindow("Received Video", cv::WINDOW_AUTOSIZE);
                
                std::cout << "Receiving video... Press ESC to stop." << std::endl;
                bool receiving = true;
                while (receiving) {
                    std::unique_lock<std::mutex> lock(frame_mutex);
                    
                    // Wait for frame with timeout
                    if (frame_cv.wait_for(lock, std::chrono::milliseconds(100), 
                                         [] { return !frame_queue.empty(); })) {
                        cv::Mat img = frame_queue.front();
                        frame_queue.pop();
                        lock.unlock();
                        
                        cv::imshow("Received Video", img);
                        int key = cv::waitKey(1);
                        if (key == 27) { // ESC
                            receiving = false;
                        }
                    } else {
                        lock.unlock();
                        // Check if user wants to exit via keyboard
                        int key = cv::waitKey(1);
                        if (key == 27) {
                            receiving = false;
                        }
                    }
                }
                
                cv::destroyAllWindows();
                std::cout << "Stopped receiving video." << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}