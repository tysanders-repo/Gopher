#ifndef FFMPEG_SENDER_HPP
#define FFMPEG_SENDER_HPP

#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class FFmpegSender {
private:
    int sock = -1;
    sockaddr_in dest_addr{};
    AVFormatContext* input_ctx = nullptr;
    AVCodecContext* encoder_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    int video_stream_idx = -1;

public:
    bool initialize(const std::string& dest_ip, uint16_t dest_port);
    void run();
    void sendPacket(AVPacket* pkt, uint8_t type);
    ~FFmpegSender();
};

// External display variables
extern std::queue<cv::Mat> display_queue;
extern std::mutex display_mutex;
extern std::condition_variable display_cv;

// Display thread function
void displayThread();

#endif // FFMPEG_SENDER_HPP