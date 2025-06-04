// src/main.cpp
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <thread>


std::vector<cv::Mat> frames_pool;
std::vector<std::thread> threads_pool;

int main() {
    // Open the default camera (device 0). On macOS, this uses AVFoundation under the hood.
    cv::VideoCapture cap(1);
    if (!cap.isOpened()) {
        std::cerr << "ERROR: Could not open camera\n";
        return -1;
    }

    // Optionally set resolution and FPS
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 30);

    cv::Mat frame;
    while (true) {
        cap.read(frame);                     // Grab a new frame
        if (frame.empty()) break;

        frames_pool.push_back(frame.clone());

        if (frames_pool.size() > 120) {
            break;
        }
    }

    for (auto& f : frames_pool) {
      cv::imshow("Frame", f);
      cv::waitKey(30);
    }
    return 0;
}
