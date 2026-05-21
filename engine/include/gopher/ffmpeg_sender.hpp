#ifndef FFMPEG_SENDER_HPP
#define FFMPEG_SENDER_HPP

#include <iostream>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class GopherClient;

class FFmpegSender {
public:
    bool initialize(const std::string& dest_ip, uint16_t dest_port, GopherClient& client);
    void run();
    void sendPacket(AVPacket* pkt, uint8_t type);
    ~FFmpegSender();

private:
    GopherClient* client_{nullptr};
    int sock{-1};
    sockaddr_in dest_addr{};
    AVCodecContext* encoder_ctx{nullptr};
    SwsContext* sws_ctx{nullptr};
    int video_stream_idx{-1};
};

#endif // FFMPEG_SENDER_HPP
