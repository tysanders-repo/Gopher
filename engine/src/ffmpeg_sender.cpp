#include "gopher/ffmpeg_sender.hpp"
#include "gopher/gopher_client_lib.hpp"
#include <cstdlib>
#include <thread>

extern "C" {
#include <libavdevice/avdevice.h>
}

struct VideoPacket {
    uint32_t size;
    uint8_t type; // 1 = video, 2 = audio
    std::vector<uint8_t> data;
};

bool FFmpegSender::initialize(const std::string& dest_ip, uint16_t dest_port, GopherClient& client) {
    client_ = &client;
    avdevice_register_all();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip.c_str(), &dest_addr.sin_addr);

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

    const char* dev = std::getenv("GOPHER_AVFOUNDATION_DEVICE");
#if defined(__APPLE__)
    const char* camera_url = (dev && dev[0] != '\0') ? dev : "0:";
#else
    (void)dev;
    const char* camera_url = "0:";
#endif

    if (avformat_open_input(&client_->input_ctx_, camera_url, input_fmt, &options) < 0) {
        std::cerr << "Failed to open camera" << std::endl;
        return false;
    }

    if (avformat_find_stream_info(client_->input_ctx_, nullptr) < 0) {
        std::cerr << "Failed to find stream info" << std::endl;
        return false;
    }

    for (int i = 0; i < client_->input_ctx_->nb_streams; i++) {
        if (client_->input_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {
        std::cerr << "No video stream found" << std::endl;
        return false;
    }

    const AVCodec* encoder = avcodec_find_encoder_by_name("h264_videotoolbox");
    if (!encoder) {
        encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        std::cout << "Using software encoder" << std::endl;
    } else {
        std::cout << "Using hardware encoder (VideoToolbox)" << std::endl;
    }

    encoder_ctx = avcodec_alloc_context3(encoder);
    encoder_ctx->width     = client_->video_width_;
    encoder_ctx->height    = client_->video_height_;
    encoder_ctx->time_base = {1, 30};
    encoder_ctx->framerate = {30, 1};
    encoder_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
    encoder_ctx->bit_rate  = 2000000;
    encoder_ctx->gop_size  = 30;
    encoder_ctx->max_b_frames = 0;

    AVDictionary* enc_opts = nullptr;
    if (strcmp(encoder->name, "h264_videotoolbox") == 0) {
        av_dict_set(&enc_opts, "realtime", "1", 0);
        av_dict_set(&enc_opts, "quality",  "0.5", 0);
    } else {
        av_dict_set(&enc_opts, "preset",        "ultrafast", 0);
        av_dict_set(&enc_opts, "tune",           "zerolatency", 0);
        av_dict_set(&enc_opts, "rc-lookahead",  "0", 0);
        av_dict_set(&enc_opts, "bf",             "0", 0);
    }

    if (avcodec_open2(encoder_ctx, encoder, &enc_opts) < 0) {
        std::cerr << "Failed to open encoder" << std::endl;
        return false;
    }

    AVCodecParameters* par = client_->input_ctx_->streams[video_stream_idx]->codecpar;
    sws_ctx = sws_getContext(
        par->width, par->height, (AVPixelFormat)par->format,
        client_->video_width_, client_->video_height_, AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );

    return true;
}

void FFmpegSender::run() {
    const double base_fps = 30.0;
    double current_fps = base_fps;
    auto frame_duration = std::chrono::microseconds(static_cast<int64_t>(1000000 / current_fps));

    auto last_stats_time = std::chrono::steady_clock::now();
    int frames_sent   = 0;
    int dropped_frames = 0;

    AVPacket* input_pkt  = av_packet_alloc();
    AVPacket* output_pkt = av_packet_alloc();
    AVFrame*  raw_frame  = av_frame_alloc();
    AVFrame*  yuv_frame  = av_frame_alloc();

    yuv_frame->format = AV_PIX_FMT_YUV420P;
    yuv_frame->width  = client_->video_width_;
    yuv_frame->height = client_->video_height_;
    av_frame_get_buffer(yuv_frame, 0);

    const AVCodec* decoder = avcodec_find_decoder(
        client_->input_ctx_->streams[video_stream_idx]->codecpar->codec_id);
    AVCodecContext* decoder_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_ctx, client_->input_ctx_->streams[video_stream_idx]->codecpar);
    avcodec_open2(decoder_ctx, decoder, nullptr);

    int64_t frame_count = 0;
    auto last_frame_time = std::chrono::steady_clock::now();

    while (!client_->send_should_stop_) {
        auto loop_start = std::chrono::steady_clock::now();
        bool frame_processed = false;

        int ret = av_read_frame(client_->input_ctx_, input_pkt);
        if (ret < 0 || client_->send_should_stop_) {
            if (ret == AVERROR_EOF) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (input_pkt->stream_index == video_stream_idx) {
            ret = avcodec_send_packet(decoder_ctx, input_pkt);
            if (ret >= 0) {
                ret = avcodec_receive_frame(decoder_ctx, raw_frame);
                if (ret >= 0) {
                    auto current_time   = std::chrono::steady_clock::now();
                    auto time_since_last = current_time - last_frame_time;

                    if (time_since_last >= frame_duration * 0.8) {
                        sws_scale(sws_ctx,
                                  raw_frame->data, raw_frame->linesize, 0, raw_frame->height,
                                  yuv_frame->data, yuv_frame->linesize);

                        yuv_frame->pts = frame_count++;

                        ret = avcodec_send_frame(encoder_ctx, yuv_frame);
                        if (ret >= 0) {
                            while (avcodec_receive_packet(encoder_ctx, output_pkt) >= 0) {
                                sendPacket(output_pkt, 1);
                                av_packet_unref(output_pkt);
                            }
                        }

                        last_frame_time  = current_time;
                        frame_processed  = true;
                        frames_sent++;
                    } else {
                        dropped_frames++;
                    }
                }
            }
        }

        av_packet_unref(input_pkt);

        if (frame_processed) {
            auto processing_time = std::chrono::steady_clock::now() - loop_start;
            auto remaining_time  = frame_duration - processing_time;
            if (remaining_time > std::chrono::microseconds(0) &&
                remaining_time < std::chrono::milliseconds(50)) {
                std::this_thread::sleep_for(remaining_time);
            }
        }

        auto current_time = std::chrono::steady_clock::now();
        if (current_time - last_stats_time >= std::chrono::seconds(1)) {
            double drop_rate = static_cast<double>(dropped_frames) / (frames_sent + dropped_frames);
            if (drop_rate > 0.1) {
                current_fps   = std::max(15.0, current_fps * 0.9);
                frame_duration = std::chrono::microseconds(static_cast<int64_t>(1000000 / current_fps));
                printf("Adapted FPS to %.1f (drop rate: %.1f%%)\n", current_fps, drop_rate * 100);
            } else if (drop_rate < 0.02 && current_fps < base_fps) {
                current_fps   = std::min(base_fps, current_fps * 1.05);
                frame_duration = std::chrono::microseconds(static_cast<int64_t>(1000000 / current_fps));
            }
            frames_sent    = 0;
            dropped_frames = 0;
            last_stats_time = current_time;
        }
    }

    // Flush encoder
    avcodec_send_frame(encoder_ctx, nullptr);
    while (avcodec_receive_packet(encoder_ctx, output_pkt) >= 0) {
        sendPacket(output_pkt, 1);
        av_packet_unref(output_pkt);
    }

    avcodec_free_context(&decoder_ctx);
    av_frame_free(&raw_frame);
    av_frame_free(&yuv_frame);
    av_packet_free(&input_pkt);
    av_packet_free(&output_pkt);
}

void FFmpegSender::sendPacket(AVPacket* pkt, uint8_t type) {
    auto     now     = std::chrono::steady_clock::now();
    uint64_t ts_us   = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    uint64_t net_ts  = htonll(ts_us);

    uint32_t payload_len = pkt->size;
    uint32_t total_size  = payload_len + 1 + sizeof(net_ts);
    uint32_t net_size    = htonl(total_size);

    sendto(sock, &net_size, sizeof(net_size), 0, (sockaddr*)&dest_addr, sizeof(dest_addr));
    sendto(sock, &type,     sizeof(type),     0, (sockaddr*)&dest_addr, sizeof(dest_addr));
    sendto(sock, &net_ts,   sizeof(net_ts),   0, (sockaddr*)&dest_addr, sizeof(dest_addr));

    const size_t MAX_CHUNK = 1400;
    size_t sent = 0;
    while (sent < payload_len) {
        size_t chunk = std::min(MAX_CHUNK, (size_t)(payload_len - sent));
        sendto(sock, pkt->data + sent, chunk, 0, (sockaddr*)&dest_addr, sizeof(dest_addr));
        sent += chunk;
    }
}

FFmpegSender::~FFmpegSender() {
    if (sws_ctx)     sws_freeContext(sws_ctx);
    if (encoder_ctx) avcodec_free_context(&encoder_ctx);
    if (sock >= 0)   close(sock);
}
