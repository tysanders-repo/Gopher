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
#include <SDL2/SDL.h>

// Fix for macOS Block.h issue - include this before FFmpeg headers
// #ifdef __APPLE__
// #define __STDC_CONSTANT_MACROS
// #define __STDC_FORMAT_MACROS
// #define __STDC_LIMIT_MACROS
// #endif

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

#define RED "\033[31m"
#define GREEN "\033[32m"
#define RESET "\033[0m"

// Declare external variables from ffmpeg_sender.cpp
extern std::queue<cv::Mat> display_queue;
extern std::mutex display_mutex;
extern std::condition_variable display_cv;

// Global variables for legacy functions
std::string gopher_name;
uint16_t listening_port;
std::vector<std::thread> threads;
std::mutex gopher_mutex;
std::queue<AVFrame*> frame_queue;
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

bool GopherClient::create_listening_socket(uint16_t& out_port) {
      listening_socket_ = socket(AF_INET, SOCK_DGRAM, 0); //inital creation of the socket that should match the broadcasted port
      if (listening_socket_ < 0) return -1;
      
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(out_port); // use the port we're sharing over the broadcast
      addr.sin_addr.s_addr = INADDR_ANY;

      if (bind(listening_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
          close(listening_socket_);
          return false;
      }

      socklen_t len = sizeof(addr);
      if (getsockname(listening_socket_, (struct sockaddr*)&addr, &len) < 0) {
          close(listening_socket_);
          return false;
      }

      //if the out_port is given 0, we should set our member var out_port to the port the system assigned
      if (out_port == 0) {
          out_port = ntohs(addr.sin_port);
      }
      
      // enforce the port to be the one we broadcasted
      if (out_port != ntohs(addr.sin_port)) {
          std::cerr << RED << "Listening socket port mismatch: expected " << out_port 
                    << ", got " << ntohs(addr.sin_port) << RESET << std::endl;
          close(listening_socket_);
          return false;
      } 

      return true;
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

// GopherClient class implementation
GopherClient::GopherClient() :
    initialized_(false),
    broadcasting_(false),
    in_call_(false),
    dev_mode_(false),
    listening_socket_(-1),
    listening_port_(0),
    display_thread_should_stop_(false) {
}

GopherClient::~GopherClient() {
    shutdown();
}

bool GopherClient::initialize(const std::string& name, uint16_t recv_port) {
    if (initialized_) {
        return false;
    }
    
    gopher_name_ = name;
    local_ip_ = get_local_ip();
    
    // Create listening socket on the port we've been given
    int success = create_listening_socket(recv_port);
    if (success < 0) {
        std::cerr << RED << "Failed to create listening socket" << RESET << std::endl;
        return false;
    }

    listening_port_ = recv_port;

    if (dev_mode_){
      sockaddr_in addr{};
      socklen_t len = sizeof(addr);

      getsockname(listening_socket_, (struct sockaddr*)&addr, &len);
      uint16_t dev_port = ntohs(addr.sin_port);

      std::cout << GREEN << "Initialized CPP backend gopherclient with:" <<
                    " name: " << gopher_name_ << "\n" <<
                    " local IP: " << local_ip_ << "\n" <<
                    " listening port: " << recv_port << "\n" <<

                    "\n\n" <<

                    "datum should match port used:" <<
                     RESET <<

                    RED << "recv == created port: " << (recv_port == dev_port) << RESET << std::endl;
    

      if (recv_port != dev_port) {
          std::cerr << RED << "Listening port mismatch: expected " << recv_port 
                    << ", got " << dev_port << RESET << std::endl;
          close(listening_socket_);
          listening_socket_ = -1;
          return false;
      }

    }

    
    initialized_ = true;
    
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
    
    in_call_ = true;
    display_thread_should_stop_ = false;
    
    // Store call target info for display window title
    call_target_name_ = target_ip + ":" + std::to_string(target_port);
    
    // Start sender and receiver threads
    sender_thread_ = std::thread(&GopherClient::ffmpeg_sending_thread, this, target_ip, target_port);
    receiver_thread_ = std::thread(&GopherClient::ffmpeg_listener_thread, this);
    
    std::cout << "Starting call to " << target_ip << ":" << target_port << std::endl;
    
    return true;
}

void GopherClient::end_call() {
    if (!in_call_) return;
    
    in_call_ = false;
    display_thread_should_stop_ = true;
    
    // Wake up display thread
    display_cv.notify_all();
    
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

int video_width = 640; // Default video width
int video_height = 480; // Default video height

void GopherClient::process_video_display() {
    if (!in_call_) return;

    // Initialize SDL video subsystem
    SDL_Init(SDL_INIT_VIDEO);

    // Create window and renderer
    SDL_Window* window = SDL_CreateWindow(
        ("Call: " + call_target_name_).c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        video_width, video_height, 0
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Create a YUV420P streaming texture
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        video_width, video_height
    );

    bool running = true;
    while (running) {
        // Handle SDL events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                break;
            }
        }

        // Pop next frame from queue if available
        AVFrame* frame = nullptr;
        {
            std::lock_guard<std::mutex> lock(display_mutex);
            if (!frame_queue.empty()) {
                frame = frame_queue.front();
                frame_queue.pop();
            }
        }

        if (!frame) {
            // No frame ready; small sleep to avoid busy loop
            SDL_Delay(10);
            continue;
        }

        // Upload YUV planes into texture
        SDL_UpdateYUVTexture(
            texture, nullptr,
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]
        );

        // Render the texture
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // Free the frame now that it's displayed
        av_frame_free(&frame);
    }

    // Cleanup SDL
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

std::string GopherClient::get_local_ip() {
    return ::get_local_ip();
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

//TODO
int GopherClient::listen_for_incoming_calls() {
    return 0;
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