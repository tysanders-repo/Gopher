#ifndef FFMPEG_RECEIVER_HPP
#define FFMPEG_RECEIVER_HPP

#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class GopherClient;

class FFmpegReceiver {
public:
    bool initialize(int existing_sock_fd, uint16_t listen_port, GopherClient& client);
    void run();
    void processVideoPacket(const std::vector<uint8_t>& data);
    ~FFmpegReceiver();

private:
    GopherClient* client_{nullptr};
    int sock{-1};
    AVCodecContext* decoder_ctx{nullptr};
    SwsContext* sws_ctx{nullptr};
};

#endif // FFMPEG_RECEIVER_HPP
