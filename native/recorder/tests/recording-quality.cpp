#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include "VideoRecorder.hpp"
extern "C" {
#include <libavformat/avformat.h>
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string directory = argv[1];
  const uint64_t start = 17000000000000000ULL;
  int64_t prior = 0;
  for (int quality : {60, 70, 80, 90}) {
    auto recorder = createFfmpegRecorder(quality);
    std::string name = "quality-" + std::to_string(quality);
    auto err = recorder->openVideoStream(directory, name, 640, 360, 30, start);
    if (!err.empty()) { std::cerr << err; return 1; }
    std::mt19937 random(123);
    for (int i = 0; i < 60; ++i) {
      auto frame = std::make_shared<Frame>(640, 360, Frame::YUV420P);
      for (int j = 0; j < 640*360; ++j) frame->data[j] = 16 + random()%220;
      std::fill(frame->data + 640*360, frame->data + 640*360*3/2, 128);
      frame->timestamp = start + i * 333333ULL;
      frame->frame_rate_N = 30; frame->frame_rate_D = 1;
      err = recorder->writeVideoFrame(frame);
      if (!err.empty()) { std::cerr << err; return 2; }
    }
    err = recorder->stop();
    if (!err.empty()) { std::cerr << err; return 3; }
    std::string path = directory + "/" + name + ".mp4";
    AVFormatContext *ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) return 5;
    if (avformat_find_stream_info(ctx, nullptr) < 0) return 6;
    const int64_t size = avio_size(ctx->pb);
    if (size <= prior) return 4;
    prior = size;
    auto packet = av_packet_alloc();
    int frames = 0; int64_t last = -1;
    while (av_read_frame(ctx, packet) >= 0) {
      if (packet->pts <= last) return 7;
      last = packet->pts; ++frames; av_packet_unref(packet);
    }
    av_packet_free(&packet); avformat_close_input(&ctx);
    if (frames != 60) return 8;
    std::cout << "quality=" << quality << " bytes=" << size << " frames=" << frames << std::endl;
  }
}
