// FrameCompressor.hpp
#ifndef FRAME_COMPRESSOR_HPP
#define FRAME_COMPRESSOR_HPP

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavutil/opt.h>
  #include <libavutil/imgutils.h>
  #include <libavutil/pixfmt.h>
  #include <libswscale/swscale.h>
}

#include <vector>
#include <stdexcept>
#include <opencv2/opencv.hpp>

class FrameCompressor {
public:
    // width, height: dimensions of incoming raw frames
    // pix_fmt: pixel format of incoming raw frames (e.g., AV_PIX_FMT_YUV420P)
    // fps: frame rate
    // bitrate: target video bitrate in bits/sec
    FrameCompressor(int width, int height, AVPixelFormat pix_fmt, int fps, int bitrate);
    ~FrameCompressor();

    // Accepts an AVFrame (allocated and filled by caller) and compresses it.
    // Compressed data is appended to internal buffer_.
    void compressFrame(AVFrame *frame);

    AVFrame* interpretFrame(cv::Mat &frameBGR);

    // After compression (including draining), getBuffer() returns all compressed data so far.
    const std::vector<uint8_t>& getBuffer() const { return buffer_; }

private:
    const AVCodec*        codec_      = nullptr;
    AVCodecContext*       ctx_        = nullptr;
    AVPacket*             pkt_        = nullptr;
    struct SwsContext*    swsCtx_     = nullptr;
    int                   frameCount_ = 0;
    std::vector<uint8_t>  buffer_;

    // Called in dtor to flush delayed packets
    void flushEncoder();
};
#endif // FRAME_COMPRESSOR_HPP
