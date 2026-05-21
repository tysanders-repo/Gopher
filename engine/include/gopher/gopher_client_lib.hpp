#ifndef GOPHER_CLIENT_LIB_HPP
#define GOPHER_CLIENT_LIB_HPP

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>

struct AVFormatContext;
struct AVFrame;

struct Gopher {
    std::string name;
    std::string ip;
    uint16_t port;
};

class FFmpegSender;
class FFmpegReceiver;

class GopherClient {
    friend class FFmpegSender;
    friend class FFmpegReceiver;

public:
    GopherClient();
    ~GopherClient();

    bool initialize(const std::string& name, uint16_t listening_port = 0);
    void shutdown();

    bool start_call(const std::string& target_ip, uint16_t target_port);
    void end_call();
    bool is_in_call() const;

    // Must be called from the main thread on macOS
    void process_video_display();

    void set_incoming_call_callback(std::function<bool(const std::string&, const std::string&, uint16_t)> callback);

    std::vector<Gopher> get_available_gophers();

    std::string get_name() const { return gopher_name_; }
    std::string get_ip()  const { return local_ip_; }
    uint16_t    get_port() const { return listening_port_; }

    void enable_dev_mode(bool enable) { dev_mode_ = enable; }

private:
    // Identity / network
    std::string gopher_name_;
    std::string local_ip_;
    uint16_t    listening_port_;
    std::string call_target_name_;

    std::atomic<bool> initialized_;
    std::atomic<bool> in_call_;
    bool dev_mode_;

    int listening_socket_;
    std::thread sender_thread_;
    std::thread receiver_thread_;
    std::thread listen_thread_;

    std::function<bool(const std::string&, const std::string&, uint16_t)> incoming_call_callback_;

    // Media state (formerly global)
    std::mutex              display_mutex_;
    std::queue<AVFrame*>    frame_queue_;
    std::atomic<bool>       send_should_stop_{false};
    std::atomic<bool>       recv_should_stop_{false};
    std::atomic<bool>       main_should_stop_{false};
    AVFormatContext*        input_ctx_{nullptr};
    int                     video_width_{1280};
    int                     video_height_{720};

    std::string get_local_ip();
    bool create_listening_socket(uint16_t& out_port);
    int  listen_for_incoming_calls();
    void ffmpeg_sending_thread(const std::string& ip, uint16_t port);
    void ffmpeg_listener_thread();

    void handle_incoming_call_request(const std::string& caller_name,
                                      const std::string& caller_ip,
                                      uint16_t caller_port);
};

// C-style interface for Rust/bridge binding
extern "C" {
    typedef struct GopherHandle GopherHandle;

    GopherHandle* gopher_create();
    void          gopher_destroy(GopherHandle* handle);

    int  gopher_initialize(GopherHandle* handle, const char* name, uint16_t port);
    void gopher_shutdown(GopherHandle* handle);

    int  gopher_start_call(GopherHandle* handle, const char* ip, uint16_t port);
    void gopher_end_call(GopherHandle* handle);
    int  gopher_is_in_call(GopherHandle* handle);

    void gopher_process_video_display(GopherHandle* handle);
    void gopher_enable_dev_mode(GopherHandle* handle, int enable);

    const char* gopher_get_name(GopherHandle* handle);
    const char* gopher_get_ip(GopherHandle* handle);
    uint16_t    gopher_get_port(GopherHandle* handle);

    typedef int (*incoming_call_callback_t)(const char* name, const char* ip, uint16_t port);
    void gopher_set_incoming_call_callback(GopherHandle* handle, incoming_call_callback_t callback);

    const char* gopher_get_available_gophers(GopherHandle* handle);
}

#endif // GOPHER_CLIENT_LIB_HPP
