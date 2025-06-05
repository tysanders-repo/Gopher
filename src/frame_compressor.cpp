#include "frame_compressor.h"
#include <iostream>





FrameCompressor::FrameCompressor(int width, int height, AVPixelFormat pix_fmt, int fps, int bitrate) {


    //sws will help convert BGR24 frames to YUV420P, which ffmpeg expects
    swsCtx_ = sws_getContext(
        width, height, AV_PIX_FMT_BGR24,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    if (!swsCtx_)
        throw std::runtime_error("Could not initialize sws_getContext");

    codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec_)
        throw std::runtime_error("H.264 encoder not found");

    ctx_ = avcodec_alloc_context3(codec_);
    if (!ctx_)
        throw std::runtime_error("Failed to allocate codec context");

    ctx_->width       = width;
    ctx_->height      = height;
    ctx_->pix_fmt     = pix_fmt;
    ctx_->time_base   = AVRational{1, fps};
    ctx_->framerate   = AVRational{fps, 1};
    ctx_->bit_rate    = bitrate;
    ctx_->gop_size    = fps;                 // one I-frame per second
    ctx_->max_b_frames= 0;                   // no B-frames for simplicity
    av_opt_set(ctx_->priv_data, "preset", "fast", 0);

    if (avcodec_open2(ctx_, codec_, nullptr) < 0)
        throw std::runtime_error("Failed to open codec");

    pkt_ = av_packet_alloc();
    if (!pkt_)
        throw std::runtime_error("Failed to allocate packet");
}

FrameCompressor::~FrameCompressor() {
    sws_freeContext(swsCtx_);
    flushEncoder();
    av_packet_free(&pkt_);
    avcodec_free_context(&ctx_);
}


AVFrame* FrameCompressor::interpretFrame(cv::Mat &frameBGR) {

    AVFrame *avFrame = av_frame_alloc();
        avFrame->format = AV_PIX_FMT_YUV420P;
        avFrame->width  = frameBGR.cols;
        avFrame->height = frameBGR.rows;
        av_frame_get_buffer(avFrame, 32);

    uint8_t* inData[4] = { frameBGR.data, nullptr, nullptr, nullptr };
    int  inLinesize[4] = { static_cast<int>(frameBGR.step[0]), 0, 0, 0 };

    sws_scale(
            swsCtx_,
            inData,
            inLinesize,
            0,
            frameBGR.rows,
            avFrame->data,
            avFrame->linesize
  );

  return avFrame;
}

void FrameCompressor::compressFrame(AVFrame *frame) {
    // Assign a presentation timestamp
    frame->pts = frameCount_++;

    if (avcodec_send_frame(ctx_, frame) < 0)
        throw std::runtime_error("Error sending frame to encoder");

    while (true) {
        int ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            throw std::runtime_error("Error receiving packet from encoder");

        // Append packet data to buffer_
        buffer_.insert(buffer_.end(), pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_);
    }
}

void FrameCompressor::flushEncoder() {
    avcodec_send_frame(ctx_, nullptr);
    while (true) {
        int ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            throw std::runtime_error("Error during encoder flush");
        buffer_.insert(buffer_.end(), pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_);
    }
}
