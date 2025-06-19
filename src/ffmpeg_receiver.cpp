#include "ffmpeg_receiver.hpp"

// External declarations - these are defined in ffmpeg_sender.cpp
extern std::queue<cv::Mat> display_queue;
extern std::mutex display_mutex;
extern std::condition_variable display_cv;

bool FFmpegReceiver::initialize(int existing_sock_fd, uint16_t listen_port) {
    // Setup decoder
    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    decoder_ctx = avcodec_alloc_context3(decoder);
    
    if (avcodec_open2(decoder_ctx, decoder, nullptr) < 0) {
        std::cerr << "Failed to open decoder" << std::endl;
        return false;
    }
    
    // Setup network
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    sock = existing_sock_fd;
    
    return true;
}

void FFmpegReceiver::run() {
    std::vector<uint8_t> packet_buffer;
    uint8_t recv_buffer[2048];
    
    while (true) {
        // Receive packet size
        uint32_t packet_size;
        if (recvfrom(sock, &packet_size, sizeof(packet_size), 0, nullptr, nullptr) 
            != sizeof(packet_size)) continue;
        
        packet_size = ntohl(packet_size);
        if (packet_size > 100000) continue; // Sanity check
        
        // Receive packet type
        uint8_t packet_type;
        if (recvfrom(sock, &packet_type, 1, 0, nullptr, nullptr) != 1) continue;
        
        // Receive packet data
        packet_buffer.clear();
        packet_buffer.reserve(packet_size - 1);
        
        while (packet_buffer.size() < packet_size - 1) {
            int n = recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);
            if (n <= 0) break;
            
            size_t copy_size = std::min((size_t)n, packet_size - 1 - packet_buffer.size());
            packet_buffer.insert(packet_buffer.end(), recv_buffer, recv_buffer + copy_size);
        }
        
        if (packet_buffer.size() >= packet_size - 1 && packet_type == 1) {
            processVideoPacket(packet_buffer);
        }
    }
}

void FFmpegReceiver::processVideoPacket(const std::vector<uint8_t>& data) {
    AVPacket* pkt = av_packet_alloc();
    pkt->data = const_cast<uint8_t*>(data.data());
    pkt->size = data.size();
    
    if (avcodec_send_packet(decoder_ctx, pkt) >= 0) {
        AVFrame* frame = av_frame_alloc();
        while (avcodec_receive_frame(decoder_ctx, frame) >= 0) {
            // Convert to BGR for OpenCV display
            cv::Mat img(frame->height, frame->width, CV_8UC3);
            
            if (!sws_ctx) {
                sws_ctx = sws_getContext(
                    frame->width, frame->height, (AVPixelFormat)frame->format,
                    frame->width, frame->height, AV_PIX_FMT_BGR24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
            }
            
            uint8_t* dst_data[1] = { img.data };
            int dst_linesize[1] = { (int)img.step[0] };
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
                      dst_data, dst_linesize);
            
            // Add to display queue - use the external global queue
            {
                std::lock_guard<std::mutex> lock(display_mutex);
                if (display_queue.size() > 10) display_queue.pop(); // Prevent overflow
                display_queue.push(img.clone());
            }
            display_cv.notify_one();
        }
        av_frame_free(&frame);
    }
    
    av_packet_free(&pkt);
}

FFmpegReceiver::~FFmpegReceiver() {
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (decoder_ctx) avcodec_free_context(&decoder_ctx);
    if (sock >= 0) close(sock);
}