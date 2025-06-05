#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <thread>

#include "frame_compressor.h"

int main() {
    // 1) Open camera (BGR output)
    cv::VideoCapture cap(0);
  
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 30);
    
    // 3) Create compressor (expects planar YUV420P)
    FrameCompressor compressor(640, 480, AV_PIX_FMT_YUV420P, 30, 4000000);

    cv::Mat frameBGR;
    int frameCount = 0;

    while (true) {
        cap.read(frameBGR);
        if (frameBGR.empty())
            break;

        AVFrame *avFrame = compressor.interpretFrame(frameBGR);

        avFrame->pts = frameCount++;
        // compressor.compressFrame(avFrame);
        // av_frame_free(&avFrame);

        cv::imshow("Camera (BGR)", frameBGR);
        if (cv::waitKey(1) == 27) // press ESC to quit
            break;

        // You can also monitor how many bytes of H.264 you have so far:
        const auto& buf = compressor.getBuffer();
        std::cout << "Total compressed bytes so far: " << buf.size() << "\r" << std::flush;
    }

    return 0;
}
