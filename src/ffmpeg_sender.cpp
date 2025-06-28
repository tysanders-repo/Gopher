#include "ffmpeg_sender.hpp"
#include <thread>

// Packet structure for network transmission
struct VideoPacket {
    uint32_t size;
    uint8_t type; // 1 = video, 2 = audio
    std::vector<uint8_t> data;
};

// Global frame queue for display - make sure these are properly defined
std::queue<cv::Mat> display_queue;
std::mutex display_mutex;
std::condition_variable display_cv;

bool FFmpegSender::initialize(const std::string& dest_ip, uint16_t dest_port) {
    // Initialize FFmpeg
    avdevice_register_all();
    
    // Setup network
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip.c_str(), &dest_addr.sin_addr);
    
        // Find input format for video device (e.g., "avfoundation" on macOS, "v4l2" on Linux)
    #if defined(__APPLE__)
        const AVInputFormat* input_fmt = av_find_input_format("avfoundation");
    #elif defined(_WIN32)
        const AVInputFormat* input_fmt = av_find_input_format("dshow");
    #else
        const AVInputFormat* input_fmt = av_find_input_format("v4l2");
    #endif
    
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", "1280x720", 0);
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "pixel_format", "uyvy422", 0);
    
    if (avformat_open_input(&input_ctx, "0:", input_fmt, &options) < 0) {
        std::cerr << "Failed to open camera" << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(input_ctx, nullptr) < 0) {
        std::cerr << "Failed to find stream info" << std::endl;
        return false;
    }
    
    // Find video stream
    for (int i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    
    if (video_stream_idx == -1) {
        std::cerr << "No video stream found" << std::endl;
        return false;
    }
    
    // Setup hardware encoder (VideoToolbox on macOS)
    const AVCodec* encoder = avcodec_find_encoder_by_name("h264_videotoolbox");
    if (!encoder) {
        // Fallback to software encoder
        encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        std::cout << "Using software encoder" << std::endl;
    } else {
        std::cout << "Using hardware encoder (VideoToolbox)" << std::endl;
    }
    
    //encoder parameters
    encoder_ctx = avcodec_alloc_context3(encoder);
    encoder_ctx->width = 1280;
    encoder_ctx->height = 720;
    encoder_ctx->time_base = {1, 30};
    encoder_ctx->framerate = {30, 1};
    encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_ctx->bit_rate = 2000000; // 2 Mbps
    encoder_ctx->gop_size = 30;
    encoder_ctx->max_b_frames = 0; // Low latency
    
    // Hardware encoder specific options
    AVDictionary* enc_opts = nullptr;
    if (strcmp(encoder->name, "h264_videotoolbox") == 0) {
        av_dict_set(&enc_opts, "realtime", "1", 0);
        av_dict_set(&enc_opts, "quality", "0.5", 0);
    } else {
        av_dict_set(&enc_opts, "preset", "ultrafast", 0);
        av_dict_set(&enc_opts, "tune", "zerolatency", 0);
        av_dict_set(&enc_opts, "rc-lookahead", "0", 0);
        av_dict_set(&enc_opts, "bf", "0", 0);
    }
    
    if (avcodec_open2(encoder_ctx, encoder, &enc_opts) < 0) {
        std::cerr << "Failed to open encoder" << std::endl;
        return false;
    }
    
    // Setup scaling context for format conversion
    AVCodecParameters* par = input_ctx->streams[video_stream_idx]->codecpar;
    sws_ctx = sws_getContext(
        par->width, par->height, (AVPixelFormat)par->format,
        1280, 720, AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );
    
    return true;
}

void FFmpegSender::run() {
    // Adaptive throttling - starts conservative and adjusts
    const double base_fps = 30.0;
    double current_fps = base_fps;
    auto frame_duration = std::chrono::microseconds(static_cast<int64_t>(1000000 / current_fps));
    
    // Performance monitoring
    auto last_stats_time = std::chrono::steady_clock::now();
    int frames_sent = 0;
    int dropped_frames = 0;
    
    // Existing allocations
    AVPacket* input_pkt = av_packet_alloc();
    AVPacket* output_pkt = av_packet_alloc();
    AVFrame* raw_frame = av_frame_alloc();
    AVFrame* yuv_frame = av_frame_alloc();
    
    // Allocate YUV frame buffer
    yuv_frame->format = AV_PIX_FMT_YUV420P;
    yuv_frame->width = 1280;
    yuv_frame->height = 720;
    av_frame_get_buffer(yuv_frame, 0);
    
    // Setup decoder for input stream
    const AVCodec* decoder = avcodec_find_decoder(input_ctx->streams[video_stream_idx]->codecpar->codec_id);
    AVCodecContext* decoder_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_ctx, input_ctx->streams[video_stream_idx]->codecpar);
    avcodec_open2(decoder_ctx, decoder, nullptr);
    
    int64_t frame_count = 0;
    auto last_frame_time = std::chrono::steady_clock::now();
    
    while (true) {
        auto loop_start = std::chrono::steady_clock::now();
        bool frame_processed = false;
        
        // Try to read and process a frame
        int ret = av_read_frame(input_ctx, input_pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // End of file - for live streams, you might want to reconnect
                break;
            }
            // Other errors - short sleep and continue
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        
        if (input_pkt->stream_index == video_stream_idx) {
            ret = avcodec_send_packet(decoder_ctx, input_pkt);
            if (ret >= 0) {
                ret = avcodec_receive_frame(decoder_ctx, raw_frame);
                if (ret >= 0) {
                    // Check if we should process this frame (drop frames if behind)
                    auto current_time = std::chrono::steady_clock::now();
                    auto time_since_last = current_time - last_frame_time;
                    
                    if (time_since_last >= frame_duration * 0.8) { // Process if 80% of frame time has passed
                        // Convert to YUV420P for encoder
                        sws_scale(sws_ctx,
                                 raw_frame->data, raw_frame->linesize, 0, raw_frame->height,
                                 yuv_frame->data, yuv_frame->linesize);
                        
                        yuv_frame->pts = frame_count++;
                        
                        // Encode frame
                        ret = avcodec_send_frame(encoder_ctx, yuv_frame);
                        if (ret >= 0) {
                            while (avcodec_receive_packet(encoder_ctx, output_pkt) >= 0) {
                                sendPacket(output_pkt, 1);
                                av_packet_unref(output_pkt);
                            }
                        }
                        
                        last_frame_time = current_time;
                        frame_processed = true;
                        frames_sent++;
                    } else {
                        // Drop this frame to catch up
                        dropped_frames++;
                    }
                }
            }
        }
        
        av_packet_unref(input_pkt);
        
        // Adaptive throttling based on performance
        if (frame_processed) {
            auto processing_time = std::chrono::steady_clock::now() - loop_start;
            auto remaining_time = frame_duration - processing_time;
            
            if (remaining_time > std::chrono::microseconds(0) && 
                remaining_time < std::chrono::milliseconds(50)) { // Max 50ms sleep
                std::this_thread::sleep_for(remaining_time);
            }
        }
        
        // Adjust frame rate every second based on performance
        auto current_time = std::chrono::steady_clock::now();
        if (current_time - last_stats_time >= std::chrono::seconds(1)) {
            double drop_rate = static_cast<double>(dropped_frames) / (frames_sent + dropped_frames);
            
            if (drop_rate > 0.1) { // If more than 10% frames dropped, reduce FPS
                current_fps = std::max(15.0, current_fps * 0.9);
                frame_duration = std::chrono::microseconds(static_cast<int64_t>(1000000 / current_fps));
                printf("Adapted FPS to %.1f (drop rate: %.1f%%)\n", current_fps, drop_rate * 100);
            } else if (drop_rate < 0.02 && current_fps < base_fps) {
                current_fps = std::min(base_fps, current_fps * 1.05);
                frame_duration = std::chrono::microseconds(static_cast<int64_t>(1000000 / current_fps));
            }
            
            // Reset counters
            frames_sent = 0;
            dropped_frames = 0;
            last_stats_time = current_time;
        }
    }
    
    // Flush the encoder
    avcodec_send_frame(encoder_ctx, nullptr);
    while (avcodec_receive_packet(encoder_ctx, output_pkt) >= 0) {
        sendPacket(output_pkt, 1);
        av_packet_unref(output_pkt);
    }
    
    // Cleanup
    avcodec_free_context(&decoder_ctx);
    av_frame_free(&raw_frame);
    av_frame_free(&yuv_frame);
    av_packet_free(&input_pkt);
    av_packet_free(&output_pkt);
}

void FFmpegSender::sendPacket(AVPacket* pkt, uint8_t type) {

    auto now = std::chrono::steady_clock::now();
    uint64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    uint64_t net_ts = htonll(ts_us);

    uint32_t payload_len = pkt->size;
    uint32_t total_size = payload_len + 1 + sizeof(net_ts);
    uint32_t net_size = htonl(total_size);

    // Send packet size + type header
    sendto(sock, &net_size, sizeof(net_size), 0,
           (sockaddr*)&dest_addr, sizeof(dest_addr));
    sendto(sock, &type, sizeof(type), 0,
           (sockaddr*)&dest_addr, sizeof(dest_addr));
    sendto(sock, &net_ts, sizeof(net_ts), 0,
           (sockaddr*)&dest_addr, sizeof(dest_addr));
    
    // Send packet data in chunks to avoid UDP size limits
    const size_t MAX_CHUNK = 1400;
    size_t sent = 0;
    while (sent < payload_len) {
        size_t chunk = std::min(MAX_CHUNK, (size_t)(payload_len - sent));
        sendto(sock, pkt->data + sent, chunk, 0,
               (sockaddr*)&dest_addr, sizeof(dest_addr));
        sent += chunk;
    }
}

FFmpegSender::~FFmpegSender() {
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (encoder_ctx) avcodec_free_context(&encoder_ctx);
    if (input_ctx) avformat_close_input(&input_ctx);
    if (sock >= 0) close(sock);
}