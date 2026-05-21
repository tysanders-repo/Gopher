#include "gopher/gopher_client_lib.hpp"
#include "gopher/ffmpeg_sender.hpp"
#include "gopher/ffmpeg_receiver.hpp"
#include <SDL2/SDL.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#ifdef __APPLE__
#include <VideoToolbox/VideoToolbox.h>
#endif

#define RED    "\033[31m"
#define GREEN  "\033[32m"
#define RESET  "\033[0m"

static std::string get_local_ip_impl() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "127.0.0.1";

    std::string ip = "127.0.0.1";
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port   = htons(80);

    if (inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr) > 0 &&
        connect(sock, (struct sockaddr*)&remote, sizeof(remote)) == 0) {
        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(sock, (struct sockaddr*)&local, &len) == 0) {
            char ip_str[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &local.sin_addr, ip_str, sizeof(ip_str)))
                ip = std::string(ip_str);
        }
    }

    close(sock);
    return ip;
}

static void on_signal(int /*signal*/) {
    SDL_Event e;
    e.type = SDL_QUIT;
    SDL_PushEvent(&e);
}

// ── GopherClient ─────────────────────────────────────────────────────────────

GopherClient::GopherClient()
    : initialized_(false),
      in_call_(false),
      dev_mode_(false),
      listening_socket_(-1),
      listening_port_(0) {}

GopherClient::~GopherClient() {}

bool GopherClient::create_listening_socket(uint16_t& out_port) {
    listening_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (listening_socket_ < 0) return false;

    // Allow immediate rebind after an unclean exit (kill -9, crash, etc.)
    int yes = 1;
    setsockopt(listening_socket_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(out_port);
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

    if (out_port == 0)
        out_port = ntohs(addr.sin_port);

    if (out_port != ntohs(addr.sin_port)) {
        std::cerr << RED << "Listening socket port mismatch: expected " << out_port
                  << ", got " << ntohs(addr.sin_port) << RESET << std::endl;
        close(listening_socket_);
        return false;
    }

    return true;
}

bool GopherClient::initialize(const std::string& name, uint16_t recv_port) {
    if (initialized_) return false;

    gopher_name_ = name;
    local_ip_    = get_local_ip_impl();

    if (!create_listening_socket(recv_port)) {
        std::cerr << RED << "Failed to create listening socket" << RESET << std::endl;
        return false;
    }

    listening_port_ = recv_port;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    if (dev_mode_) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        getsockname(listening_socket_, (struct sockaddr*)&addr, &len);
        uint16_t dev_port = ntohs(addr.sin_port);

        std::cout << GREEN
                  << "Initialized GopherClient:"
                  << " name=" << gopher_name_
                  << " ip=" << local_ip_
                  << " port=" << recv_port
                  << RESET << std::endl;

        if (recv_port != dev_port) {
            std::cerr << RED << "Listening port mismatch: expected " << recv_port
                      << ", got " << dev_port << RESET << std::endl;
            close(listening_socket_);
            listening_socket_ = -1;
            return false;
        }
    }

    send_should_stop_ = false;
    recv_should_stop_ = false;
    main_should_stop_ = false;
    initialized_      = true;

    return true;
}

void GopherClient::shutdown() {
    main_should_stop_ = true;
    recv_should_stop_ = true;
    send_should_stop_ = true;

    // Wake anyone blocked on frame_cv (if we add one later)

    initialized_ = false;
    avformat_close_input(&input_ctx_);

    if (sender_thread_.joinable())   sender_thread_.join();
    if (receiver_thread_.joinable()) receiver_thread_.join();
}

bool GopherClient::start_call(const std::string& target_ip, uint16_t target_port) {
    if (!initialized_ || in_call_) return false;

    in_call_           = true;
    main_should_stop_  = false;
    call_target_name_  = target_ip + ":" + std::to_string(target_port);

    sender_thread_   = std::thread(&GopherClient::ffmpeg_sending_thread, this, target_ip, target_port);
    receiver_thread_ = std::thread(&GopherClient::ffmpeg_listener_thread, this);

    std::cerr << "Starting call to " << target_ip << ":" << target_port << std::endl;
    return true;
}

void GopherClient::end_call() {
    main_should_stop_ = true;
    recv_should_stop_ = true;
    send_should_stop_ = true;

    if (sender_thread_.joinable())   sender_thread_.join();
    if (receiver_thread_.joinable()) receiver_thread_.join();
}

bool GopherClient::is_in_call() const {
    return in_call_;
}

void GopherClient::set_incoming_call_callback(
    std::function<bool(const std::string&, const std::string&, uint16_t)> callback) {
    incoming_call_callback_ = callback;
}

void GopherClient::process_video_display() {
    if (!in_call_) return;

    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow(
        "Gopher Video Call",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        640, 360,
        SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture   = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        video_width_, video_height_
    );

    SDL_Rect dst = {0, 0, 640, 360};

    while (!main_should_stop_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                main_should_stop_ = true;
                break;
            }
        }

        AVFrame* frame = nullptr;
        {
            std::lock_guard<std::mutex> lock(display_mutex_);
            if (!frame_queue_.empty()) {
                frame = frame_queue_.front();
                frame_queue_.pop();
            }
        }

        if (!frame) {
            SDL_Delay(10);
            continue;
        }

        SDL_UpdateYUVTexture(
            texture, nullptr,
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]
        );

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_RenderPresent(renderer);

        av_frame_free(&frame);
    }

    if (texture)  SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);

    Uint32 quit_time = SDL_GetTicks() + 50;
    while (SDL_GetTicks() < quit_time) {
        SDL_PumpEvents();
        SDL_Delay(1);
    }

    SDL_Quit();
}

std::string GopherClient::get_local_ip() {
    return get_local_ip_impl();
}

int GopherClient::listen_for_incoming_calls() {
    return 0; // TODO
}

void GopherClient::ffmpeg_sending_thread(const std::string& ip, uint16_t port) {
    FFmpegSender sender;
    if (sender.initialize(ip, port, *this)) {
        sender.run();
    } else {
        std::cerr << "[sender] init failed" << std::endl;
        shutdown();
    }
}

void GopherClient::ffmpeg_listener_thread() {
    FFmpegReceiver receiver;
    if (receiver.initialize(listening_socket_, listening_port_, *this)) {
        receiver.run();
    } else {
        std::cerr << "[receiver] init failed" << std::endl;
        shutdown();
    }
}

void GopherClient::handle_incoming_call_request(const std::string& caller_name,
                                                 const std::string& caller_ip,
                                                 uint16_t caller_port) {
    if (incoming_call_callback_) {
        bool accept = incoming_call_callback_(caller_name, caller_ip, caller_port);
        if (accept)
            start_call(caller_ip, caller_port);
    }
}
