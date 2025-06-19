#include <iostream>
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

int main() {
    avdevice_register_all();

    const AVInputFormat* input_fmt = av_find_input_format("avfoundation");
    if (!input_fmt) {
        std::cerr << "avfoundation not found\n";
        return 1;
    }

    AVFormatContext* fmt_ctx = nullptr;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", "1920x1080", 0);
    av_dict_set(&options, "framerate", "30", 0);

    if (avformat_open_input(&fmt_ctx, "0:", input_fmt, &options) < 0) {
        std::cerr << "Failed to open video input\n";
        return 1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Failed to find stream info\n";
        return 1;
    }

    int video_stream_index = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        std::cerr << "No video stream found\n";
        return 1;
    }

    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codecpar);
    avcodec_open2(codec_ctx, codec, nullptr);

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();

    // Prepare scaler to convert whatever pixel format to BGR for OpenCV
    SwsContext* sws = sws_getContext(
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
        codec_ctx->width, codec_ctx->height, AV_PIX_FMT_BGR24,
        SWS_BICUBIC, nullptr, nullptr, nullptr
    );

    int dst_stride[1] = { 3 * codec_ctx->width };
    std::vector<uint8_t> buffer(dst_stride[0] * codec_ctx->height);
    uint8_t* dst_data[1] = { buffer.data() };

    std::cout << "Press ESC to exit.\n";

    while (true) {
        if (av_read_frame(fmt_ctx, pkt) < 0) continue;

        if (pkt->stream_index == video_stream_index) {
            avcodec_send_packet(codec_ctx, pkt);
            while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                sws_scale(sws,
                          frame->data, frame->linesize,
                          0, codec_ctx->height,
                          dst_data, dst_stride);

                cv::Mat img(codec_ctx->height, codec_ctx->width, CV_8UC3, dst_data[0], dst_stride[0]);
                cv::imshow("Live", img);
                if (cv::waitKey(1) == 27) goto done;  // ESC
            }
        }

        av_packet_unref(pkt);
    }

done:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    sws_freeContext(sws);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return 0;
}
