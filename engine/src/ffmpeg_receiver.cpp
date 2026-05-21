#include "gopher/ffmpeg_receiver.hpp"
#include "gopher/frame_reassembler.hpp"
#include "gopher/gopher_client_lib.hpp"
#include "gopher/packet.hpp"

#include <cstring>
#include <iostream>

bool FFmpegReceiver::initialize(int socket_fd, uint16_t /*listen_port*/, GopherClient& client) {
    client_ = &client;

    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    decoder_ctx = avcodec_alloc_context3(decoder);

    if (!decoder || !decoder_ctx) {
        std::cerr << "Failed to find or allocate decoder" << std::endl;
        return false;
    }

    if (avcodec_open2(decoder_ctx, decoder, nullptr) < 0) {
        std::cerr << "Failed to open decoder" << std::endl;
        return false;
    }

    sock = socket_fd;
    struct timeval tv{.tv_sec = 0, .tv_usec = 50000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return true;
}

void FFmpegReceiver::run() {
    uint8_t buf[gopher::MAX_DATAGRAM + 64];
    gopher::FrameReassembler reasm;
    gopher::FrameReassembler::Output frame;

    // Wait for the first keyframe before feeding the decoder anything —
    // otherwise the H.264 decoder spews "non-existing PPS 0 referenced" until
    // it sees an IDR. We can drop early P-frames safely on UDP.
    bool seen_keyframe = false;

    while (!client_->recv_should_stop_) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            break;
        }

        using R = gopher::FrameReassembler::Result;
        const R r = reasm.ingest(buf, (std::size_t)n, frame);
        if (r != R::FrameReady) continue;

        if (frame.type != gopher::PKT_VIDEO) continue;

        if (!seen_keyframe) {
            if (!frame.is_keyframe) continue;
            seen_keyframe = true;
        }

        processVideoPacket(frame.data);
    }
}

static constexpr int MAX_QUEUE_FRAMES = 10;

void FFmpegReceiver::processVideoPacket(const std::vector<uint8_t>& data) {
    AVPacket* pkt = av_packet_alloc();
    pkt->data = static_cast<uint8_t*>(av_malloc(data.size()));
    memcpy(pkt->data, data.data(), data.size());
    pkt->size = data.size();

    if (avcodec_send_packet(decoder_ctx, pkt) >= 0) {
        AVFrame* raw = av_frame_alloc();
        while (avcodec_receive_frame(decoder_ctx, raw) >= 0) {
            AVFrame* frame = av_frame_clone(raw);
            std::lock_guard<std::mutex> lock(client_->display_mutex_);
            if ((int)client_->frame_queue_.size() > MAX_QUEUE_FRAMES) {
                av_frame_free(&client_->frame_queue_.front());
                client_->frame_queue_.pop();
            }
            client_->frame_queue_.push(frame);
        }
        av_frame_free(&raw);
    }
    av_packet_free(&pkt);
}

FFmpegReceiver::~FFmpegReceiver() {
    if (sws_ctx)     sws_freeContext(sws_ctx);
    if (decoder_ctx) avcodec_free_context(&decoder_ctx);
    if (sock >= 0)   close(sock);
}
