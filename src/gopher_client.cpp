#include <iostream>
#include <vector>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <mutex>
#include <fcntl.h>
#include <chrono>

//video specific includes
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// my stuff :)
#include "gopherd_helper.hpp"


struct Gopher {
  std::string name;
  std::string ip;
  uint16_t port;
};

Gopher me_gopher;
Gopher them_gopher;


std::string gopher_name;
uint16_t listening_port;
std::vector<std::thread> threads;
std::mutex gopher_mutex;

std::queue<cv::Mat> frame_queue;
std::mutex frame_mutex;
std::condition_variable frame_cv;
pid_t gopherd_pid = -1;


/* 
  defintely not my original code, common pattern to get local IP address
*/
std::string get_local_ip() {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  std::string ip = "127.0.0.1";

  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(80);

  inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

  if (connect(sock, (struct sockaddr*)&remote, sizeof(remote)) == 0) {
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (getsockname(sock, (struct sockaddr*)&local, &len) == 0) {
      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &local.sin_addr, ip_str, sizeof(ip_str));
      ip = std::string(ip_str);
    }
  }

  close(sock);
  return std::string(ip);
}

char getch() {
  termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO); // disable buffering and echo
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  char ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  return ch;
}

int broadcast(){
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  int broadcast_enable = 1;

  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

  sockaddr_in addr{}; 
  addr.sin_family = AF_INET;
  addr.sin_port = htons(43753);
  addr.sin_addr.s_addr = inet_addr("255.255.255.255");


  std::string message = "name:" + gopher_name + ";ip:" + get_local_ip() + ";port:" +  std::to_string(listening_port) + ";";

  while(true){

    // send the broadcast message
    // std::cout << "Broadcasting: " << message << "\n";
    sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
    sleep(5);
  }
  
}

std::vector<Gopher> query_daemon_for_gophers() {
  std::vector<Gopher> result;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return result;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(DAEMON_PORT);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
    char buffer[2048];
    int n = read(sock, buffer, sizeof(buffer) - 1);
    if (n > 0) {
      buffer[n] = '\0';

      std::istringstream iss(buffer);
      std::string line;
      while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string name, ip, port_str;
        if (std::getline(ls, name, ',') &&
            std::getline(ls, ip, ',') &&
            std::getline(ls, port_str)) {
          result.push_back(Gopher{name, ip, static_cast<uint16_t>(std::stoi(port_str))});
        }
      }
    }
  }
  close(sock);
  return result;
}


int create_listening_socket(uint16_t& out_port) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0); //let OS choose
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(sock, (struct sockaddr*)&addr, sizeof(addr));

  socklen_t len = sizeof(addr);
  getsockname(sock, (struct sockaddr*)&addr, &len);
  out_port = ntohs(addr.sin_port);

  return sock;
}

void listener_thread(int sock) {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext* dec_ctx = avcodec_alloc_context3(codec);
    avcodec_open2(dec_ctx, codec, nullptr);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    SwsContext* sws = nullptr;
    int dst_width = 0, dst_height = 0;

    sockaddr_in sender_addr{};
    socklen_t sender_len = sizeof(sender_addr);
    uint8_t recvbuf[65536];

    while (true) {
        int n = recvfrom(sock, recvbuf, sizeof(recvbuf), 0,
                         (sockaddr*)&sender_addr, &sender_len);
        if (n <= 0) continue;

        // Wrap received data into packet
        pkt->data = recvbuf;
        pkt->size = n;

        if (avcodec_send_packet(dec_ctx, pkt) < 0) continue;
        while (avcodec_receive_frame(dec_ctx, frame) == 0) {
            if (!sws) {
                dst_width  = frame->width;
                dst_height = frame->height;
                sws = sws_getContext(
                    dst_width, dst_height, (AVPixelFormat)frame->format,
                    dst_width, dst_height, AV_PIX_FMT_BGR24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
            }
            int dst_stride = 3 * dst_width;
            std::vector<uint8_t> dstbuf(dst_stride * dst_height);
            uint8_t* dst_data[1] = { dstbuf.data() };
            int dst_linesize[1] = { dst_stride };

            sws_scale(sws,
                      frame->data, frame->linesize,
                      0, dst_height,
                      dst_data, dst_linesize);

            cv::Mat img(dst_height, dst_width, CV_8UC3, dstbuf.data(), dst_stride);
            if (!img.empty()) {
                std::lock_guard<std::mutex> lock(frame_mutex);
                frame_queue.push(img.clone());
                frame_cv.notify_one();
            }
        }
    }

    // Cleanup (unreachable in loop)
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
}


void sending_thread(const std::string& ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    connect(sock, (sockaddr*)&addr, sizeof(addr));

    // Setup capture input
    avdevice_register_all();
    const AVInputFormat* input_fmt = av_find_input_format("avfoundation");
    AVFormatContext* fmt_ctx = nullptr;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "framerate", "30", 0);
    av_dict_set(&opts, "video_size", "640x480", 0);
    avformat_open_input(&fmt_ctx, "0:", input_fmt, &opts);
    avformat_find_stream_info(fmt_ctx, nullptr);
    int vid_idx = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i)
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            vid_idx = i;
    AVCodecParameters* cp = fmt_ctx->streams[vid_idx]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(cp->codec_id);
    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, cp);
    avcodec_open2(dec_ctx, dec, nullptr);

    // Setup H.264 encoder
    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
    enc_ctx->width      = dec_ctx->width;
    enc_ctx->height     = dec_ctx->height;
    enc_ctx->pix_fmt    = AV_PIX_FMT_YUV420P;
    enc_ctx->time_base  = AVRational{1,30};
    enc_ctx->bit_rate   = 400000;
    avcodec_open2(enc_ctx, enc, nullptr);

    SwsContext* rgb2yuv = sws_getContext(
        enc_ctx->width, enc_ctx->height, AV_PIX_FMT_BGR24,
        enc_ctx->width, enc_ctx->height, enc_ctx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    AVPacket* pkt_in  = av_packet_alloc();
    AVFrame*  frame   = av_frame_alloc();
    AVFrame*  yuv     = av_frame_alloc();
    yuv->format = enc_ctx->pix_fmt;
    yuv->width  = enc_ctx->width;
    yuv->height = enc_ctx->height;
    av_frame_get_buffer(yuv, 0);

    AVPacket* pkt_out = av_packet_alloc();

    while (av_read_frame(fmt_ctx, pkt_in) >= 0) {
        if (pkt_in->stream_index != vid_idx) {
            av_packet_unref(pkt_in);
            continue;
        }
        avcodec_send_packet(dec_ctx, pkt_in);
        while (avcodec_receive_frame(dec_ctx, frame) == 0) {
            // Convert BGR (from capture) to YUV420P
            sws_scale(rgb2yuv,
                      frame->data, frame->linesize,
                      0, enc_ctx->height,
                      yuv->data, yuv->linesize);

            avcodec_send_frame(enc_ctx, yuv);
            while (avcodec_receive_packet(enc_ctx, pkt_out) == 0) {
                send(sock, pkt_out->data, pkt_out->size, 0);
                av_packet_unref(pkt_out);
            }
        }
        av_packet_unref(pkt_in);
    }

    // Cleanup
    sws_freeContext(rgb2yuv);
    av_frame_free(&yuv);
    av_frame_free(&frame);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_packet_free(&pkt_out);
    close(sock);
}



int main() {
  ensure_daemon_running("./gopherd");

  std::vector<std::string> menu = {"Exit"};
  int selected = 0;

  int listening_socket = create_listening_socket(listening_port);

  std::cout << "Thank you for using Gopher! Please provide a friendly name for your Gopher:\n";
  std::getline(std::cin, gopher_name);
  std::string my_ip = get_local_ip();
  std::cout << "ip addr: " << my_ip << "\n";

  me_gopher.name = gopher_name;
  me_gopher.ip =  my_ip;
  me_gopher.port = listening_port;

  threads.emplace_back(broadcast);
  threads.emplace_back(listener_thread, listening_socket);

  while (true) {
    system("clear");

    auto gophers = query_daemon_for_gophers();
    std::cerr << "[DEBUG] Got " << gophers.size() << " gophers from daemon.\n";
    for (const auto& g : gophers) {
        std::cerr << "  -> " << g.name << " at " << g.ip << ":" << g.port << "\n";
    }

    menu.clear();
    menu.push_back("Exit");
    for (const auto& gopher : gophers) {
      menu.push_back(gopher.name + " (" + gopher.ip + ":" + std::to_string(gopher.port) + ")");
    }

    for (int i = 0; i < menu.size(); i++) {
      if (i == selected)
        std::cout << "> " << menu[i] << "\n";
      else
        std::cout << "  " << menu[i] << "\n";
    }

    if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) == -1) {
      perror("fcntl");
    }

    char c = getch();
    if (c == 27) {
      getch();
      char arrow = getch();
      if (arrow == 'A') selected = (selected - 1 + menu.size()) % menu.size();
      if (arrow == 'B') selected = (selected + 1) % menu.size();
    } else if (c == '\n') {
      std::cout << "You selected: " << menu[selected] << "\n";
      if (menu[selected] == "Exit") break;

      Gopher selected_gopher;
      for (const auto& gopher : gophers) {
        std::string gopher_display = gopher.name + " (" + gopher.ip + ":" + std::to_string(gopher.port) + ")";
        if (menu[selected] == gopher_display) {
          selected_gopher = gopher;
          break;
        }
      }

      threads.emplace_back(sending_thread, selected_gopher.ip, selected_gopher.port);

      cv::namedWindow("Received", cv::WINDOW_AUTOSIZE);
      while (true) {
        std::unique_lock<std::mutex> lock(frame_mutex);
        frame_cv.wait(lock, [] { return !frame_queue.empty(); });
        cv::Mat img = frame_queue.front();
        frame_queue.pop();
        lock.unlock();

        cv::imshow("Received", img);
        if (cv::waitKey(1) == 27) break;
      }
      cv::destroyAllWindows();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (gopherd_pid > 0) {
  #ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, gopherd_pid);
    if (h) {
      TerminateProcess(h, 0);
      CloseHandle(h);
    }
  #else
    kill(gopherd_pid, SIGTERM);
  #endif
  }

  return 0;
}
