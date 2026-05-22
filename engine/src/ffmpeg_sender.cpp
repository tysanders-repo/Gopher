#include "gopher/ffmpeg_sender.hpp"
#include "gopher/gopher_client_lib.hpp"
#include "gopher/packet.hpp"
#include <cstdlib>
#include <cstring>
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
    avformat_network_init();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip.c_str(), &dest_addr.sin_addr);

    // Absorb encoder output bursts (a single IDR is easily 100KB+).
    // macOS silently caps at kern.ipc.maxsockbuf; that's fine.
    int sbuf = 4 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sbuf, sizeof(sbuf));

    // GOPHER_TEST_PATTERN, if set, is a libavfilter source spec (e.g.
    // "testsrc2=size=1280x720:rate=30" or "mandelbrot=size=1280x720:rate=30").
    // When present we skip the camera entirely — useful for CI, headless dev,
    // and any environment without AVFoundation TCC + run-loop context.
    const char* test_pattern = std::getenv("GOPHER_TEST_PATTERN");

    const AVInputFormat* input_fmt = nullptr;
    const char* input_url = nullptr;
    AVDictionary* options = nullptr;

    if (test_pattern && test_pattern[0] != '\0') {
        input_fmt = av_find_input_format("lavfi");
        input_url = test_pattern;
        std::cerr << "[sender] test pattern: " << test_pattern << std::endl;
    } else {
#if defined(__APPLE__)
        input_fmt = av_find_input_format("avfoundation");
#elif defined(_WIN32)
        input_fmt = av_find_input_format("dshow");
#else
        input_fmt = av_find_input_format("v4l2");
#endif
        av_dict_set(&options, "video_size", "1280x720", 0);
        av_dict_set(&options, "framerate", "30", 0);
        av_dict_set(&options, "pixel_format", "uyvy422", 0);

        const char* dev = std::getenv("GOPHER_AVFOUNDATION_DEVICE");
#if defined(__APPLE__)
        input_url = (dev && dev[0] != '\0') ? dev : "0:";
#else
        (void)dev;
        input_url = "0:";
#endif
    }

    if (avformat_open_input(&client_->input_ctx_, input_url, input_fmt, &options) < 0) {
        std::cerr << "Failed to open input '" << input_url << "'" << std::endl;
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
        std::cerr << "Using software encoder" << std::endl;
    } else {
        std::cerr << "Using hardware encoder (VideoToolbox)" << std::endl;
    }

    encoder_ctx = avcodec_alloc_context3(encoder);
    encoder_ctx->width     = client_->video_width_;
    encoder_ctx->height    = client_->video_height_;
    encoder_ctx->time_base = {1, 30};
    encoder_ctx->framerate = {30, 1};
    encoder_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
    encoder_ctx->bit_rate  = 2000000;
    encoder_ctx->gop_size  = 15;  // 0.5s IDR cadence → faster recovery from loss
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
    // Hard 30 fps capture cadence. Eliminates the lavfi runaway problem (the
    // synthetic sources have no internal rate limit) and gives consistent
    // wall-clock spacing for the receiver-side latency CSV.
    constexpr auto target_period = std::chrono::microseconds(1'000'000 / 30);
    auto next_capture = std::chrono::steady_clock::now();

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

    int64_t pts_counter = 0;

    while (!client_->send_should_stop_) {
        // Pace the capture loop.
        auto now = std::chrono::steady_clock::now();
        if (now < next_capture) {
            std::this_thread::sleep_for(next_capture - now);
        }
        next_capture += target_period;
        // If we fell more than 5 frames behind (e.g. encoder stalled), resync
        // rather than spinning trying to catch up.
        if (std::chrono::steady_clock::now() - next_capture > target_period * 5) {
            next_capture = std::chrono::steady_clock::now() + target_period;
        }

        int ret = av_read_frame(client_->input_ctx_, input_pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) break;
            av_packet_unref(input_pkt);
            continue;
        }
        if (input_pkt->stream_index != video_stream_idx) {
            av_packet_unref(input_pkt);
            continue;
        }

        // capture_ts is the moment the frame becomes ours — used end-to-end
        // for the latency CSV. Set once and propagated to every fragment.
        const uint64_t capture_ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        ret = avcodec_send_packet(decoder_ctx, input_pkt);
        if (ret >= 0 && avcodec_receive_frame(decoder_ctx, raw_frame) >= 0) {
            sws_scale(sws_ctx,
                      raw_frame->data, raw_frame->linesize, 0, raw_frame->height,
                      yuv_frame->data, yuv_frame->linesize);
            yuv_frame->pts = pts_counter++;

            if (avcodec_send_frame(encoder_ctx, yuv_frame) >= 0) {
                while (avcodec_receive_packet(encoder_ctx, output_pkt) >= 0) {
                    const bool is_key = (output_pkt->flags & AV_PKT_FLAG_KEY) != 0;
                    sendPacket(output_pkt, gopher::PKT_VIDEO, is_key, capture_ts_us);
                    av_packet_unref(output_pkt);
                }
            }
        }

        av_packet_unref(input_pkt);
    }

    // Flush encoder.
    avcodec_send_frame(encoder_ctx, nullptr);
    const uint64_t flush_ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    while (avcodec_receive_packet(encoder_ctx, output_pkt) >= 0) {
        const bool is_key = (output_pkt->flags & AV_PKT_FLAG_KEY) != 0;
        sendPacket(output_pkt, gopher::PKT_VIDEO, is_key, flush_ts);
        av_packet_unref(output_pkt);
    }

    avcodec_free_context(&decoder_ctx);
    av_frame_free(&raw_frame);
    av_frame_free(&yuv_frame);
    av_packet_free(&input_pkt);
    av_packet_free(&output_pkt);
}

void FFmpegSender::sendPacket(AVPacket* pkt, uint8_t type, bool is_keyframe,
                              uint64_t capture_ts_us) {
    using namespace gopher;

    const std::size_t total = static_cast<std::size_t>(pkt->size);
    if (total == 0) return;

    const std::size_t n_frags = (total + MAX_PAYLOAD - 1) / MAX_PAYLOAD;
    if (n_frags > 0xFFFF) {
        std::cerr << "[sender] frame too large to fragment (" << total << "B)\n";
        return;
    }

    const uint32_t frame_id     = ++frame_id_counter_;
    const uint32_t net_frame_id = htonl(frame_id);
    const uint16_t net_frag_ct  = htons(static_cast<uint16_t>(n_frags));
    const uint64_t net_ts       = htonll(capture_ts_us);
    const uint16_t net_flags    = htons(is_keyframe ? FLAG_KEY : 0);

    uint8_t buf[MAX_DATAGRAM];
    std::size_t offset   = 0;
    uint16_t    frag_idx = 0;

    while (offset < total) {
        const std::size_t chunk = std::min(MAX_PAYLOAD, total - offset);

        PacketHeader hdr{};
        hdr.version       = WIRE_VERSION;
        hdr.type          = type;
        hdr.flags         = net_flags;
        hdr.stream_id     = htons(0);
        hdr.frag_count    = net_frag_ct;
        hdr.frame_id      = net_frame_id;
        hdr.frag_id       = htons(frag_idx);
        hdr.payload_len   = htons(static_cast<uint16_t>(chunk));
        hdr.capture_ts_us = net_ts;

        std::memcpy(buf, &hdr, sizeof(hdr));
        std::memcpy(buf + sizeof(hdr), pkt->data + offset, chunk);

        sendto(sock, buf, sizeof(hdr) + chunk, 0,
               (sockaddr*)&dest_addr, sizeof(dest_addr));

        offset   += chunk;
        frag_idx += 1;
    }
}

FFmpegSender::~FFmpegSender() {
    if (sws_ctx)     sws_freeContext(sws_ctx);
    if (encoder_ctx) avcodec_free_context(&encoder_ctx);
    if (sock >= 0)   close(sock);
}
