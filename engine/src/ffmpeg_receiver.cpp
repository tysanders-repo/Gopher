#include "gopher/ffmpeg_receiver.hpp"
#include "gopher/gopher_client_lib.hpp"

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
    uint8_t recv_buffer[2048];

    while (!client_->recv_should_stop_) {
        uint32_t net_size;
        ssize_t n = recvfrom(sock, &net_size, sizeof(net_size), 0, nullptr, nullptr);
        if (n != sizeof(net_size)) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            else
                break;
        }
        uint32_t total_size = ntohl(net_size);

        uint8_t packet_type;
        recvfrom(sock, &packet_type, sizeof(packet_type), 0, nullptr, nullptr);

        uint64_t net_ts;
        recvfrom(sock, &net_ts, sizeof(net_ts), 0, nullptr, nullptr);

        constexpr uint32_t header_overhead = 1 + sizeof(net_ts);
        if (total_size < header_overhead || total_size > (1 << 24)) break;

        uint32_t payload_len = total_size - header_overhead;

        std::vector<uint8_t> payload(payload_len);
        size_t got = 0;
        while (got < payload_len) {
            int r = recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);
            if (r <= 0) break;
            size_t copy_sz = std::min((size_t)r, payload_len - got);
            std::memcpy(payload.data() + got, recv_buffer, copy_sz);
            got += copy_sz;
        }

        if (packet_type == 1)
            processVideoPacket(payload);
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
