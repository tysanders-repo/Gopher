#ifndef FFMPEG_RECEIVER_HPP
#define FFMPEG_RECEIVER_HPP

#include <iostream>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class FFmpegReceiver {
private:
    int sock = -1;
    AVCodecContext* decoder_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;

public:
    bool initialize(int existing_sock_fd, uint16_t listen_port);
    void run();
    void processVideoPacket(const std::vector<uint8_t>& data);
    ~FFmpegReceiver();
};

#endif // FFMPEG_RECEIVER_HPP