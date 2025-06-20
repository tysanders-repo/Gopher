#ifndef GOPHER_CLIENT_LIB_HPP
#define GOPHER_CLIENT_LIB_HPP

#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>

struct Gopher {
    std::string name;
    std::string ip;
    uint16_t port;
};

class GopherClient {
public:
    GopherClient();
    ~GopherClient();
    
    // Core functionality
    bool initialize(const std::string& name, uint16_t listening_port = 0);
    void shutdown();
    
    // Broadcasting
    void start_broadcasting();
    void stop_broadcasting();
    
    // Call management
    bool start_call(const std::string& target_ip, uint16_t target_port);
    void end_call();
    bool is_in_call() const;
    
    // Video display - MUST be called from main thread on macOS
    void process_video_display();
    
    // Incoming call handling
    void set_incoming_call_callback(std::function<bool(const std::string&, const std::string&, uint16_t)> callback);
    
    // Discovery
    std::vector<Gopher> get_available_gophers();
    
    // Getters
    std::string get_name() const { return gopher_name_; }
    std::string get_ip() const { return local_ip_; }
    uint16_t get_port() const { return listening_port_; }
    
    // Dev/Testing
    void enable_dev_mode(bool enable) { dev_mode_ = enable; }

private:
    std::string gopher_name_;
    std::string local_ip_;
    uint16_t listening_port_;
    std::string call_target_name_;
    
    std::atomic<bool> initialized_;
    std::atomic<bool> broadcasting_;
    std::atomic<bool> in_call_;
    std::atomic<bool> display_thread_should_stop_;
    bool dev_mode_;
    
    int listening_socket_;
    std::thread broadcast_thread_;
    std::thread sender_thread_;
    std::thread receiver_thread_;
    std::thread listen_thread_;
    
    std::function<bool(const std::string&, const std::string&, uint16_t)> incoming_call_callback_;
    
    // Helper methods
    std::string get_local_ip();
    int create_listening_socket(uint16_t& out_port);
    void broadcast_loop();
    void listen_for_incoming_calls();
    void ffmpeg_sending_thread(const std::string& ip, uint16_t port);
    void ffmpeg_listener_thread();
    
    // Protocol handling
    void handle_incoming_call_request(const std::string& caller_name, 
                                    const std::string& caller_ip, 
                                    uint16_t caller_port);
};

// C-style interface for Python binding
extern "C" {
    typedef struct GopherHandle GopherHandle;
    
    GopherHandle* gopher_create();
    void gopher_destroy(GopherHandle* handle);
    
    int gopher_initialize(GopherHandle* handle, const char* name, uint16_t port);
    void gopher_shutdown(GopherHandle* handle);
    
    void gopher_start_broadcasting(GopherHandle* handle);
    void gopher_stop_broadcasting(GopherHandle* handle);
    
    int gopher_start_call(GopherHandle* handle, const char* ip, uint16_t port);
    void gopher_end_call(GopherHandle* handle);
    int gopher_is_in_call(GopherHandle* handle);
    
    void gopher_process_video_display(GopherHandle* handle);
    
    void gopher_enable_dev_mode(GopherHandle* handle, int enable);
    
    const char* gopher_get_name(GopherHandle* handle);
    const char* gopher_get_ip(GopherHandle* handle);
    uint16_t gopher_get_port(GopherHandle* handle);
    
    // Callback for incoming calls - returns 1 to accept, 0 to reject
    typedef int (*incoming_call_callback_t)(const char* name, const char* ip, uint16_t port);
    void gopher_set_incoming_call_callback(GopherHandle* handle, incoming_call_callback_t callback);
    
    // Get available gophers (returns JSON string)
    const char* gopher_get_available_gophers(GopherHandle* handle);
}

#endif // GOPHER_CLIENT_LIB_HPP