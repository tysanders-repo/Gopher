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
#include <sstream>

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
#include "gopher_client_lib.hpp"

#ifdef __APPLE__
#include <VideoToolbox/VideoToolbox.h>
#endif

// Declare external variables from ffmpeg_sender.cpp
extern std::queue<cv::Mat> display_queue;
extern std::mutex display_mutex;
extern std::condition_variable display_cv;

// Global variables for legacy functions
std::string gopher_name;
uint16_t listening_port;
std::vector<std::thread> threads;
std::mutex gopher_mutex;

std::queue<cv::Mat> frame_queue;
std::mutex frame_mutex;
std::condition_variable frame_cv;
pid_t gopherd_pid = -1;


/* 
  Common pattern to get local IP address
*/
std::string get_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "127.0.0.1";
    
    std::string ip = "127.0.0.1";

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(80);

    if (inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr) <= 0) {
        close(sock);
        return ip;
    }

    if (connect(sock, (struct sockaddr*)&remote, sizeof(remote)) == 0) {
        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(sock, (struct sockaddr*)&local, &len) == 0) {
            char ip_str[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &local.sin_addr, ip_str, sizeof(ip_str))) {
                ip = std::string(ip_str);
            }
        }
    }

    close(sock);
    return ip;
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

int broadcast() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    int broadcast_enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        close(sock);
        return -1;
    }

    sockaddr_in addr{}; 
    addr.sin_family = AF_INET;
    addr.sin_port = htons(43753);
    addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    std::string message = "name:" + gopher_name + ";ip:" + get_local_ip() + ";port:" + std::to_string(listening_port) + ";";

    while(true) {
        if (sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Broadcast failed" << std::endl;
        }
        sleep(5);
    }
    
    close(sock);
    return 0;
}

std::vector<Gopher> query_daemon_for_gophers() {
    std::vector<Gopher> result;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return result;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DAEMON_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        close(sock);
        return result;
    }

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
                    try {
                        result.push_back(Gopher{name, ip, static_cast<uint16_t>(std::stoi(port_str))});
                    } catch (const std::exception& e) {
                        std::cerr << "Error parsing gopher data: " << e.what() << std::endl;
                    }
                }
            }
        }
    }
    close(sock);
    return result;
}

int create_listening_socket(uint16_t& out_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0); //let OS choose
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr*)&addr, &len) < 0) {
        close(sock);
        return -1;
    }
    
    out_port = ntohs(addr.sin_port);
    return sock;
}

void ffmpeg_sending_thread(const std::string& ip, uint16_t port) {
    FFmpegSender sender;
    if (sender.initialize(ip, port)) {
        std::cout << "Starting FFmpeg sender to " << ip << ":" << port << std::endl;
        sender.run();
    } else {
        std::cerr << "Failed to initialize FFmpeg sender" << std::endl;
    }
}

void ffmpeg_listener_thread(int existing_sock_fd, uint16_t listen_port) {
    FFmpegReceiver receiver;
    if (receiver.initialize(existing_sock_fd, listen_port)) {
        std::cout << "Starting FFmpeg receiver on port " << listen_port << std::endl; 
        receiver.run();
    } else {
        std::cerr << "Failed to initialize FFmpeg receiver" << std::endl;
    }
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

int direct_call_mode(const std::string& name, const std::string& ip, uint16_t port) {
    ensure_daemon_running("./gopherd");
    setup_hardware_acceleration();
    
    int listening_socket = create_listening_socket(listening_port);
    if (listening_socket < 0) {
        std::cerr << "Failed to create listening socket" << std::endl;
        return -1;
    }
    
    std::cout << "Calling " << name << " at " << ip << ":" << port << std::endl;
    std::cout << "Press ESC to end call..." << std::endl;
    
    // Start the call
    std::thread sender_thread(ffmpeg_sending_thread, ip, port);
    std::thread receiver_thread(ffmpeg_listener_thread, listening_socket, listening_port);
    
    // Display received video
    cv::namedWindow("Call with " + name, cv::WINDOW_AUTOSIZE);
    
    while (true) {
        std::unique_lock<std::mutex> lock(display_mutex);
        display_cv.wait_for(lock, std::chrono::milliseconds(100), 
                           [] { return !display_queue.empty(); });
        
        if (!display_queue.empty()) {
            cv::Mat frame = display_queue.front();
            display_queue.pop();
            lock.unlock();
            
            cv::imshow("Call with " + name, frame);
        }
        
        // Check for ESC key to end call
        int key = cv::waitKey(1);
        if (key == 27) { // ESC key
            std::cout << "Ending call..." << std::endl;
            break;
        }
    }
    
    cv::destroyAllWindows();
    
    // Clean up threads
    sender_thread.detach();
    receiver_thread.detach();
    
    return 0;
}

// GopherClient class implementation
GopherClient::GopherClient() :
    initialized_(false),
    broadcasting_(false),
    in_call_(false),
    dev_mode_(false),
    listening_socket_(-1),
    listening_port_(0) {
}

GopherClient::~GopherClient() {
    shutdown();
}

bool GopherClient::initialize(const std::string& name, uint16_t port) {
    if (initialized_) {
        return false; // Already initialized
    }
    
    gopher_name_ = name;
    local_ip_ = get_local_ip();
    
    // Create listening socket
    uint16_t actual_port = port;
    listening_socket_ = create_listening_socket(actual_port);
    if (listening_socket_ < 0) {
        std::cerr << "Failed to create listening socket" << std::endl;
        return false;
    }
    
    listening_port_ = actual_port;
    initialized_ = true;
    
    // Start listening for incoming calls
    listen_thread_ = std::thread(&GopherClient::listen_for_incoming_calls, this);
    
    return true;
}

void GopherClient::shutdown() {
    if (!initialized_) return;
    
    stop_broadcasting();
    end_call();
    
    initialized_ = false;
    
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }
    
    if (listening_socket_ >= 0) {
        close(listening_socket_);
        listening_socket_ = -1;
    }
}

void GopherClient::start_broadcasting() {
    if (!initialized_ || broadcasting_) return;
    
    broadcasting_ = true;
    broadcast_thread_ = std::thread(&GopherClient::broadcast_loop, this);
}

void GopherClient::stop_broadcasting() {
    if (!broadcasting_) return;
    
    broadcasting_ = false;
    if (broadcast_thread_.joinable()) {
        broadcast_thread_.join();
    }
}

bool GopherClient::start_call(const std::string& target_ip, uint16_t target_port) {
    if (!initialized_ || in_call_) return false;
    
    // Check if we're trying to call ourselves
    bool calling_self = (target_ip == local_ip_ && target_port == listening_port_);
    
    if (calling_self && !dev_mode_) {
        std::cerr << "Cannot call yourself unless dev mode is enabled" << std::endl;
        return false;
    }
    
    if (calling_self && dev_mode_) {
        std::cout << "Dev mode: Starting self-call (loopback test)" << std::endl;
    }
    
    in_call_ = true;
    
    // Start sender and receiver threads
    sender_thread_ = std::thread(&GopherClient::ffmpeg_sending_thread, this, target_ip, target_port);
    receiver_thread_ = std::thread(&GopherClient::ffmpeg_listener_thread, this);
    
    std::cout << "Starting call to " << target_ip << ":" << target_port << std::endl;
    
    return true;
}

void GopherClient::end_call() {
    if (!in_call_) return;
    
    in_call_ = false;
    
    if (sender_thread_.joinable()) {
        sender_thread_.join();
    }
    
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    
    cv::destroyAllWindows();
}

bool GopherClient::is_in_call() const {
    return in_call_;
}

void GopherClient::set_incoming_call_callback(std::function<bool(const std::string&, const std::string&, uint16_t)> callback) {
    incoming_call_callback_ = callback;
}

std::vector<Gopher> GopherClient::get_available_gophers() {
    std::lock_guard<std::mutex> lock(gopher_mutex);
    std::vector<Gopher> gophers = query_daemon_for_gophers();
    
    // In dev mode, add ourselves to the list for testing
    if (dev_mode_ && initialized_) {
        Gopher self_gopher;
        self_gopher.name = gopher_name_ + " (self)";
        self_gopher.ip = local_ip_;
        self_gopher.port = listening_port_;
        gophers.insert(gophers.begin(), self_gopher); // Add at the beginning
    }
    
    return gophers;
}


std::string GopherClient::get_local_ip() {
    return ::get_local_ip();
}

int GopherClient::create_listening_socket(uint16_t& out_port) {
    return ::create_listening_socket(out_port);
}

void GopherClient::broadcast_loop() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;
    
    int broadcast_enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        close(sock);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(43753);
    addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    std::string message = "name:" + gopher_name_ + ";ip:" + local_ip_ + ";port:" + std::to_string(listening_port_) + ";";

    while(broadcasting_) {
        if (sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            if (dev_mode_) {
                std::cerr << "Broadcast failed" << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    close(sock);
}

void GopherClient::listen_for_incoming_calls() {
}

void GopherClient::ffmpeg_sending_thread(const std::string& ip, uint16_t port) {
    ::ffmpeg_sending_thread(ip, port);
}

void GopherClient::ffmpeg_listener_thread() {
    ::ffmpeg_listener_thread(listening_socket_, listening_port_);
}

void GopherClient::handle_incoming_call_request(const std::string& caller_name, 
                                               const std::string& caller_ip, 
                                               uint16_t caller_port) {
    if (incoming_call_callback_) {
        bool accept = incoming_call_callback_(caller_name, caller_ip, caller_port);
        if (accept) {
            start_call(caller_ip, caller_port);
        }
    }
}