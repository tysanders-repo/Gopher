#include "gopher/ffmpeg_receiver.hpp"
#include "gopher/frame_reassembler.hpp"
#include "gopher/gopher_client_lib.hpp"
#include "gopher/packet.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

// Monotonic microseconds from steady_clock — same domain the sender stamps
// into capture_ts_us. On a single host (loopback test) this gives a direct
// end-to-end latency measurement; across two hosts the readings need NTP/PTP
// alignment before subtracting.
uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

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
    struct timeval tv{.tv_sec = 0, .tv_usec = 50'000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Absorb encoder-output bursts of fragments. macOS silently caps at
    // kern.ipc.maxsockbuf; the actual ceiling is fine for our purposes.
    int rbuf = 4 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf));

    return true;
}

void FFmpegReceiver::run() {
    uint8_t buf[gopher::MAX_DATAGRAM + 64];
    gopher::FrameReassembler reasm;
    gopher::FrameReassembler::Output frame;

    // CSV metrics — optional, controlled by env var.
    //   GOPHER_METRICS_CSV=/tmp/recv.csv
    // Columns: frame_id,capture_ts_us,first_arrived_us,complete_us,decode_us,
    //          frag_total,frag_received,complete
    std::FILE* csv = nullptr;
    if (const char* path = std::getenv("GOPHER_METRICS_CSV")) {
        csv = std::fopen(path, "w");
        if (csv) {
            std::fprintf(csv,
                "frame_id,capture_ts_us,first_arrived_us,complete_us,"
                "decode_us,frag_total,frag_received,complete\n");
            std::fflush(csv);
            std::cerr << "[receiver] metrics CSV: " << path << "\n";
        } else {
            std::cerr << "[receiver] could not open CSV at " << path << "\n";
        }
    }

    // Wait for the first keyframe before feeding the decoder — otherwise it
    // spews PPS errors on inter-frames missing the IDR context.
    bool seen_keyframe = false;
    uint64_t last_sweep_us = now_us();

    while (!client_->recv_should_stop_) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        const uint64_t arrival_us = now_us();

        // Sweep stale in-flight frames roughly every 50ms.
        if (arrival_us - last_sweep_us > 50'000) {
            reasm.sweep_timeouts(arrival_us);
            last_sweep_us = arrival_us;
        }

        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            break;
        }

        using R = gopher::FrameReassembler::Result;
        const R r = reasm.ingest(buf, (std::size_t)n, arrival_us, frame);
        if (r != R::FrameReady) continue;
        if (frame.type != gopher::PKT_VIDEO) continue;

        if (!seen_keyframe) {
            if (!frame.is_keyframe) continue;
            seen_keyframe = true;
        }

        const uint64_t complete_us = now_us();
        processVideoPacket(frame.data);
        const uint64_t decode_us = now_us();

        if (csv) {
            std::fprintf(csv,
                "%u,%llu,%llu,%llu,%llu,%u,%u,1\n",
                frame.frame_id,
                (unsigned long long)frame.capture_ts_us,
                (unsigned long long)arrival_us,   // approximation: last-frag arrival
                (unsigned long long)complete_us,
                (unsigned long long)decode_us,
                frame.frag_count,
                frame.frag_received);
            std::fflush(csv);
        }
    }

    if (csv) std::fclose(csv);

    std::cerr << "[receiver] completed=" << reasm.frames_completed()
              << " dropped="   << reasm.frames_dropped()
              << " timed_out=" << reasm.frames_timed_out() << "\n";
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
