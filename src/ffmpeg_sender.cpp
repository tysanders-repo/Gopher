#include "ffmpeg_sender.hpp"

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
    
    // Open camera input (macOS avfoundation)
    const AVInputFormat* input_fmt = av_find_input_format("avfoundation");
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
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    return true;
}

void FFmpegSender::run() {
    AVPacket* input_pkt = av_packet_alloc();
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
    
    // while (av_read_frame(input_ctx, input_pkt) >= 0) {
    while (true) {
      while (av_read_frame(input_ctx, input_pkt) >= 0) {
        if (input_pkt->stream_index == video_stream_idx) {
            // Decode input frame
            if (avcodec_send_packet(decoder_ctx, input_pkt) >= 0) {
                while (avcodec_receive_frame(decoder_ctx, raw_frame) >= 0) {
                    // Convert to YUV420P for encoder
                    sws_scale(sws_ctx, 
                            raw_frame->data, raw_frame->linesize, 0, raw_frame->height,
                            yuv_frame->data, yuv_frame->linesize);
                    
                    yuv_frame->pts = frame_count++;
                    
                    // Encode frame
                    if (avcodec_send_frame(encoder_ctx, yuv_frame) >= 0) {
                        AVPacket* enc_pkt = av_packet_alloc();
                        while (avcodec_receive_packet(encoder_ctx, enc_pkt) >= 0) {
                            sendPacket(enc_pkt, 1); // type 1 = video
                            av_packet_unref(enc_pkt);
                        }
                        av_packet_free(&enc_pkt);
                    }
                }
            }
        }
        av_packet_unref(input_pkt);
    }
  }
    
    // Cleanup
    avcodec_free_context(&decoder_ctx);
    av_frame_free(&raw_frame);
    av_frame_free(&yuv_frame);
    av_packet_free(&input_pkt);
}

void FFmpegSender::sendPacket(AVPacket* pkt, uint8_t type) {
    // Send packet size + type header
    uint32_t total_size = htonl(pkt->size + 1);
    sendto(sock, &total_size, sizeof(total_size), 0, 
           (sockaddr*)&dest_addr, sizeof(dest_addr));
    sendto(sock, &type, 1, 0, 
           (sockaddr*)&dest_addr, sizeof(dest_addr));
    
    // Send packet data in chunks to avoid UDP size limits
    const size_t MAX_CHUNK = 1400;
    size_t offset = 0;
    while (offset < pkt->size) {
        size_t chunk_size = std::min(MAX_CHUNK, (size_t)(pkt->size - offset));
        sendto(sock, pkt->data + offset, chunk_size, 0,
               (sockaddr*)&dest_addr, sizeof(dest_addr));
        offset += chunk_size;
    }
}

FFmpegSender::~FFmpegSender() {
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (encoder_ctx) avcodec_free_context(&encoder_ctx);
    if (input_ctx) avformat_close_input(&input_ctx);
    if (sock >= 0) close(sock);
}

// Display thread function
void displayThread() {
    cv::namedWindow("Received Video", cv::WINDOW_AUTOSIZE);
    
    while (true) {
        std::unique_lock<std::mutex> lock(display_mutex);
        display_cv.wait(lock, [] { return !display_queue.empty(); });
        
        cv::Mat frame = display_queue.front();
        display_queue.pop();
        lock.unlock();
        
        cv::imshow("Received Video", frame);
        if (cv::waitKey(1) == 27) break; // ESC to exit
    }
    
    cv::destroyAllWindows();
}