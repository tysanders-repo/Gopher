#include "ffmpeg_receiver.hpp"

// External declarations - these are defined in ffmpeg_sender.cpp
extern std::queue<AVFrame*> frame_queue;
extern std::mutex display_mutex;
// extern std::condition_variable display_cv; //!remove

bool FFmpegReceiver::initialize(int advertised_socket_, uint16_t listen_port) {
    // Setup decoder
    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    decoder_ctx = avcodec_alloc_context3(decoder);
    
    if (avcodec_open2(decoder_ctx, decoder, nullptr) < 0) {
        std::cerr << "Failed to open decoder" << std::endl;
        return false;
    }
    
    // Take control of the existing socket
    sock = advertised_socket_;

    //set sock to be non-blocking
    struct timeval tv{.tv_sec=0, .tv_usec=100000}; // 100 ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return true;
}

void FFmpegReceiver::run() {

    // buffer to read raw UDP data
    uint8_t recv_buffer[2048];

    while (!recv_thread_should_stop_) {
        // --- 1) Read header ---
        uint32_t net_size;
        ssize_t n = recvfrom(sock, &net_size, sizeof(net_size), 0, nullptr, nullptr);
        if (n != sizeof(net_size)) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) 
            continue;   // just a timeout, try again
        else
            break;      // fatal error or closed socket
        }
        uint32_t total_size = ntohl(net_size);


        uint8_t packet_type;
        recvfrom(sock, &packet_type, sizeof(packet_type), 0, nullptr, nullptr);

        uint64_t net_ts;
        recvfrom(sock, &net_ts, sizeof(net_ts), 0, nullptr, nullptr);
        uint64_t send_ts_us = ntohll(net_ts);

        constexpr uint32_t header_overhead = 1 + sizeof(net_ts);
        // i shall not allocate 4 gigabytes again
        if (total_size < header_overhead || total_size > (1 << 24)) break;

        uint32_t payload_len = total_size - header_overhead;

        // --- 2) Read payload ---
        std::vector<uint8_t> payload(payload_len);
        size_t got = 0;
        while (got < payload_len) {
            int r = recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);
            if (r <= 0) break;
            size_t copy_sz = std::min((size_t)r, payload_len - got);
            std::memcpy(payload.data() + got, recv_buffer, copy_sz);
            got += copy_sz;
        }

        // --- 3) Compute & record latency ---
        auto now = std::chrono::steady_clock::now();
        uint64_t recv_ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                 now.time_since_epoch()).count();
        // double latency_ms = (recv_ts_us - send_ts_us) * 1e-3;

        // latencies.push_back(latency_ms);
        // if (latencies.size() > MAX_SAMPLES)
        //     latencies.pop_front();

        // --- 5) Dispatch the packet ---
        if (packet_type == 1) {
            processVideoPacket(payload);
        }
        // (add audio processing here later)
    }
}

int MAX_QUEUE_FRAMES = 10; // Maximum frames to keep in queue

void FFmpegReceiver::processVideoPacket(const std::vector<uint8_t>& data) {
    // 1) wrap incoming bytes in an AVPacket
    AVPacket* pkt = av_packet_alloc();
    pkt->data = const_cast<uint8_t*>(data.data());
    pkt->size = data.size();

    if (avcodec_send_packet(decoder_ctx, pkt) >= 0) {
        AVFrame* raw = av_frame_alloc();
        while (avcodec_receive_frame(decoder_ctx, raw) >= 0) {
            // 2) clone frame (deep copy) so we own the data
            AVFrame* frame = av_frame_clone(raw);

            // 3) push into our thread-safe queue, dropping oldest if full
            std::lock_guard<std::mutex> lock(display_mutex);
            if (frame_queue.size() > MAX_QUEUE_FRAMES) {
                av_frame_free(&frame_queue.front());
                frame_queue.pop();
            }
            frame_queue.push(frame);
            // display_cv.notify_one();//!remove
        }
        av_frame_free(&raw);
    }
    av_packet_free(&pkt);
}


FFmpegReceiver::~FFmpegReceiver() {
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (decoder_ctx) avcodec_free_context(&decoder_ctx);
    if (sock >= 0) close(sock);
}