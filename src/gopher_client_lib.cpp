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
#include <csignal>

//video specific includes
#include <SDL2/SDL.h>

// FFmpeg includes
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// my stuff :)
#include "ffmpeg_sender.hpp"
#include "ffmpeg_receiver.hpp"
#include "gopher_client_lib.hpp"

#ifdef __APPLE__
#include <VideoToolbox/VideoToolbox.h>
#endif

#define RED "\033[31m"
#define GREEN "\033[32m"
#define RESET "\033[0m"
#define ORANGE "\033[38;5;208m"
#define BLUE "\033[34m"


extern std::mutex display_mutex;

std::atomic<bool> send_thread_should_stop_;
std::atomic<bool> recv_thread_should_stop_;
std::atomic<bool> main_thread_should_stop_;

// I just want to talk fabrice. i justttt want to talk to you.
AVFormatContext* input_ctx = nullptr;

std::string gopher_name;
uint16_t listening_port;
std::queue<AVFrame*> frame_queue;
std::vector<std::thread> threads;
std::mutex gopher_mutex;
std::mutex frame_mutex;
std::condition_variable frame_cv;
pid_t gopherd_pid = -1;

GopherClient* gopher_client = nullptr;

// Global variables for video display
int video_width = 1280;
int video_height = 720;

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
  listening_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (listening_socket_ < 0) return -1;
  
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(out_port);
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

  //if out_port was up to the system, we should replace it with the one we got
  if (out_port == 0) {
    out_port = ntohs(addr.sin_port);
  }
  
  // enforce the port to be the one we broadcasted
  if (out_port != ntohs(addr.sin_port)) {
    std::cerr << RED << "Listening socket port mismatch: expected " << out_port 
              << ", got " << ntohs(addr.sin_port)
              << RESET << std::endl;

    close(listening_socket_);
    return false;
  } 

  return true;
}

static void on_signal(int signal) {
  SDL_Event e;
  e.type = SDL_QUIT;
  SDL_PushEvent(&e);
}

void ffmpeg_sending_thread(const std::string& ip, uint16_t port) {
  FFmpegSender sender;
  if (sender.initialize(ip, port)) {
    std::cout << "Starting FFmpeg sender to " << ip << ":" << port << std::endl;
    sender.run();
  } else {
    std::cerr << "Failed to initialize FFmpeg sender" << std::endl;
    gopher_client->shutdown();
  }
}

void ffmpeg_listener_thread(int existing_sock_fd, uint16_t listen_port) {
  FFmpegReceiver receiver;
  if (receiver.initialize(existing_sock_fd, listen_port)) {
    std::cout << "Starting FFmpeg receiver on port " << listen_port << std::endl; 
    receiver.run();
  } else {
    std::cerr << "Failed to initialize FFmpeg receiver" << std::endl;
    gopher_client->shutdown();
  }
}

// GopherClient class implementation
GopherClient::GopherClient() :
  initialized_(false),
  in_call_(false),
  dev_mode_(false),
  listening_socket_(-1),
  listening_port_(0) {}

GopherClient::~GopherClient(){}

bool GopherClient::initialize(const std::string& name, uint16_t recv_port) {
    if (initialized_) return false;
    
    gopher_name_ = name;
    local_ip_ = get_local_ip();
    
    // Create listening socket on the port we've been given
    int success = create_listening_socket(recv_port);
    if (success < 0) {
      std::cerr << RED << "Failed to create listening socket" << RESET << std::endl;
      return false;
    }

    listening_port_ = recv_port;

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    if (dev_mode_){
      sockaddr_in addr{};
      socklen_t len = sizeof(addr);

      getsockname(listening_socket_, (struct sockaddr*)&addr, &len);
      uint16_t dev_port = ntohs(addr.sin_port);

      std::cout << GREEN 
                << "Initialized CPP backend gopherclient with:" <<
                " name: " << gopher_name_ << "\n" <<
                " local IP: " << local_ip_ << "\n" <<
                " listening port: " << recv_port << "\n"
                << RESET << std::endl;
    

      if (recv_port != dev_port) {
          std::cerr << RED << "Listening port mismatch: expected " << recv_port 
                    << ", got " << dev_port << RESET << std::endl;
          close(listening_socket_);
          listening_socket_ = -1;
          return false;
      }

    }

    send_thread_should_stop_ = false;
    recv_thread_should_stop_ = false;
    main_thread_should_stop_ = false;
    initialized_ = true;

    gopher_client = this;
    
    return true;
}


void GopherClient::shutdown() {    
  main_thread_should_stop_ = true;
  recv_thread_should_stop_ = true;
  send_thread_should_stop_ = true;

  frame_cv.notify_all();
  
  initialized_ = false;
  avformat_close_input(&input_ctx);

  if (sender_thread_.joinable())   sender_thread_.join();
  if (receiver_thread_.joinable()) receiver_thread_.join();
}



bool GopherClient::start_call(const std::string& target_ip, uint16_t target_port) {
    if (!initialized_ || in_call_) return false;
    
    in_call_ = true;
    main_thread_should_stop_ = false;

    // Store call target info for display window title
    call_target_name_ = target_ip + ":" + std::to_string(target_port);
    
    // Start sender and receiver threads
    sender_thread_ = std::thread(&GopherClient::ffmpeg_sending_thread, this, target_ip, target_port);
    receiver_thread_ = std::thread(&GopherClient::ffmpeg_listener_thread, this);
    
    std::cout << "Starting call to " << target_ip << ":" << target_port << std::endl;
    
    return true;
}

void GopherClient::end_call() {    
    main_thread_should_stop_ = true;
    recv_thread_should_stop_ = true;
    send_thread_should_stop_ = true;
    
    frame_cv.notify_all();

    // now join exactly once:
    if (sender_thread_.joinable())   sender_thread_.join();
    if (receiver_thread_.joinable()) receiver_thread_.join();
}

bool GopherClient::is_in_call() const {
    return in_call_;
}

void GopherClient::set_incoming_call_callback(std::function<bool(const std::string&, const std::string&, uint16_t)> callback) {
    incoming_call_callback_ = callback;
}

void GopherClient::process_video_display() {
    if (!in_call_) return;

    // Initialize SDL video subsystem
    SDL_Init(SDL_INIT_VIDEO);

    int screen_width = 640; // Default width
    int screen_height = 360; // Default height

    // Create window and renderer
    SDL_Window* window = SDL_CreateWindow(
        ("Call: " + call_target_name_).c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screen_width, screen_height, 0
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Create a YUV420P streaming texture
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        video_width, video_height
    );

    SDL_Rect dst = { 0, 0, 640, 360 };

    while (!main_thread_should_stop_) {
        // Handle SDL events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                main_thread_should_stop_ = true;
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
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_RenderPresent(renderer);

        // Free the frame now that it's displayed
        av_frame_free(&frame);
    }

    // Cleanup SDL
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);

    Uint32 quit_time = SDL_GetTicks() + 50;   // 50 ms from now
    while (SDL_GetTicks() < quit_time) {
      SDL_PumpEvents();
      SDL_Delay(1);
    }

    SDL_Quit();

    std::cout << GREEN << "[PROCCESS_VIDEO_DISPLAY]" << RESET << "finished running loop" << std::endl;
}

std::string GopherClient::get_local_ip() {
    return ::get_local_ip();
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