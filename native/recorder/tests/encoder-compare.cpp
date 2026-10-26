// Re-encode one recording through FFRecorder with the hardware encoder and with a software
// encoder at each quality setting, and report size, bitrate, PSNR against the input and
// encode speed in <input>-compare-results.txt beside the input. Each run's video is a
// temporary file, deleted once measured. With --keep the videos stay in <input>-compare,
// one folder per run (hardware-q80, software-q80, ...) because the files share timestamps,
// so they can be compared frame for frame in the review app.
//
// On Windows "hardware" is h264_mf on the GPU and "software" is Microsoft's encoder; on
// Apple Silicon they are h264_videotoolbox and libx264.
//
// Usage: encoder-compare [--keep] <input.mp4> [seconds, default 20]
// On Windows an MP4 can be dragged onto encoder-compare.exe. Build there with
// build-encoder-compare.bat; on macOS:
//   c++ -std=c++11 -DSTANDALONE -Isrc tests/encoder-compare.cpp src/FFRecorder.cpp \
//     $(pkg-config --cflags --libs libavcodec libavformat libavutil libswscale) -o encoder-compare
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <direct.h>
#define makeDir(path) _mkdir(path)
#define removeDir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define makeDir(path) mkdir(path, 0755)
#define removeDir(path) rmdir(path)
#endif
#include "SystemEventQueue.hpp"
#include "VideoRecorder.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

// Writes to the console and the results file.
struct Report
{
  std::ofstream file;
  template <class T> Report &operator<<(const T &v)
  {
    std::cout << v << std::flush;
    file << v << std::flush;
    return *this;
  }
} out;

// Decodes the video stream of an MP4 one frame at a time.
struct Reader
{
  AVFormatContext *ic = nullptr;
  AVCodecContext *dec = nullptr;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  int stream = -1;
  bool flushing = false;

  std::string open(const std::string &path)
  {
    if (avformat_open_input(&ic, path.c_str(), nullptr, nullptr) < 0) return "cannot open " + path;
    if (avformat_find_stream_info(ic, nullptr) < 0) return "cannot read " + path;
    const AVCodec *codec = nullptr;
    stream = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream < 0 || !codec) return "no video stream in " + path;
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, ic->streams[stream]->codecpar);
    if (avcodec_open2(dec, codec, nullptr) < 0) return "cannot decode " + path;
    return "";
  }
  AVStream *st() const { return ic->streams[stream]; }
  // The next decoded frame, or nullptr at the end.
  AVFrame *next()
  {
    while (true)
    {
      const int ret = avcodec_receive_frame(dec, frame);
      if (ret >= 0) return frame;
      if (ret != AVERROR(EAGAIN) || flushing) return nullptr;
      if (av_read_frame(ic, pkt) < 0)
      {
        avcodec_send_packet(dec, nullptr);
        flushing = true;
        continue;
      }
      if (pkt->stream_index == stream) avcodec_send_packet(dec, pkt);
      av_packet_unref(pkt);
    }
  }
  ~Reader()
  {
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    if (ic) avformat_close_input(&ic);
  }
};

struct Input
{
  std::string path, stem, folder;
  int width = 0, height = 0;
  float fps = 0;
  uint64_t firstTs100ns = 0; // UTC of the first frame, as the recorder stamps it
  int64_t maxRel100ns = 0;
};

struct Run
{
  std::string label, encoder, err;
  int frames = 0;
  double encodeSecs = 0, bytes = 0, psnrMean = 0, psnrMin = 0;
};

static std::string lastQualityEvent()
{
  auto events = SystemEventQueue::getEventList();
  for (auto it = events.rbegin(); it != events.rend(); ++it)
    if ((*it)->message.compare(0, 17, "Recording quality") == 0) return (*it)->message;
  return "";
}

static Run encodeRun(const Input &in, const std::string &folder, int quality, bool software)
{
  Run run;
  run.label = std::string(software ? "software" : "hardware") + "-q" + std::to_string(quality);
  const std::string dir = folder + "/" + run.label;
  makeDir(dir.c_str());
  const std::string mp4 = dir + "/" + in.stem + ".mp4";
  std::remove(mp4.c_str()); // the recorder renames into place, which fails over an existing file

  Reader reader;
  run.err = reader.open(in.path);
  if (!run.err.empty()) return run;
  auto recorder = createFfmpegRecorder(quality, software);
  run.err = recorder->openVideoStream(dir, in.stem, in.width, in.height, in.fps, in.firstTs100ns);
  if (!run.err.empty()) return run;
  const std::string event = lastQualityEvent();
  const size_t using_ = event.find(" using ");
  run.encoder = using_ == std::string::npos ? "?" : event.substr(using_ + 7);

  SwsContext *sws = nullptr;
  int64_t firstPts = AV_NOPTS_VALUE;
  double secs = 0;
  for (AVFrame *f = reader.next(); f; f = reader.next())
  {
    if (firstPts == AV_NOPTS_VALUE) firstPts = f->best_effort_timestamp;
    const int64_t rel100ns = av_rescale_q(f->best_effort_timestamp - firstPts, reader.st()->time_base,
                                          av_make_q(1, 10000000));
    if (rel100ns >= in.maxRel100ns) break;
    auto frame = std::make_shared<Frame>(in.width, in.height, Frame::YUV420P);
    uint8_t *dst[3] = {frame->data, frame->data + in.width * in.height,
                       frame->data + in.width * in.height * 5 / 4};
    const int dstStride[3] = {in.width, in.width / 2, in.width / 2};
    sws = sws_getCachedContext(sws, f->width, f->height, AVPixelFormat(f->format), in.width, in.height,
                               AV_PIX_FMT_YUV420P, SWS_POINT, nullptr, nullptr, nullptr);
    sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dstStride);
    frame->timestamp = in.firstTs100ns + rel100ns;
    frame->frame_rate_N = int(std::lround(in.fps * 1000));
    frame->frame_rate_D = 1000;
    const auto start = std::chrono::steady_clock::now();
    run.err = recorder->writeVideoFrame(frame);
    secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!run.err.empty()) break;
    ++run.frames;
  }
  sws_freeContext(sws);
  const auto start = std::chrono::steady_clock::now();
  const std::string stopErr = recorder->stop();
  run.encodeSecs = secs + std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (run.err.empty()) run.err = stopErr;
  if (!run.err.empty()) return run;

  // Keep the input's sidecar (guide lines, timing) beside the re-encoded file.
  std::ifstream sidecar(in.folder + "/" + in.stem + ".json", std::ios::binary);
  if (sidecar) std::ofstream(dir + "/" + in.stem + ".json", std::ios::binary) << sidecar.rdbuf();

  // PSNR of the luma plane against the input, frame by frame.
  Reader source, encoded;
  run.err = source.open(in.path);
  if (run.err.empty()) run.err = encoded.open(mp4);
  if (!run.err.empty()) return run;
  run.bytes = double(avio_size(encoded.ic->pb));
  double mseSum = 0, worst = 0;
  int compared = 0;
  for (int i = 0; i < run.frames; ++i)
  {
    AVFrame *a = source.next(), *b = encoded.next();
    if (!a || !b) break;
    double sq = 0;
    for (int y = 0; y < in.height; ++y)
      for (int x = 0; x < in.width; ++x)
      {
        const int d = a->data[0][y * a->linesize[0] + x] - b->data[0][y * b->linesize[0] + x];
        sq += d * d;
      }
    const double mse = sq / (double(in.width) * in.height);
    mseSum += mse;
    worst = std::max(worst, mse);
    ++compared;
  }
  if (compared != run.frames)
  {
    run.err = "encoded file has " + std::to_string(compared) + " of " + std::to_string(run.frames) + " frames";
    return run;
  }
  auto psnr = [](double mse) { return mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 99.0; };
  run.psnrMean = psnr(mseSum / compared);
  run.psnrMin = psnr(worst);
  return run;
}

static int finish(int code)
{
#if defined(_WIN32)
  // A drag-and-drop launch closes the window on exit.
  std::cout << "Press Enter to exit.";
  std::cin.get();
#endif
  return code;
}

int main(int argc, char **argv)
{
  bool keep = false;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i)
  {
    if (std::string(argv[i]) == "--keep")
      keep = true;
    else
      args.push_back(argv[i]);
  }
  if (args.empty())
  {
    std::cout << "Usage: encoder-compare [--keep] <input.mp4> [seconds, default 20]\n"
                 "On Windows, drag a recording onto encoder-compare.exe.\n";
    return finish(2);
  }
  av_log_set_level(AV_LOG_ERROR);
  Input in;
  in.path = args[0];
  std::replace(in.path.begin(), in.path.end(), '\\', '/');
  const size_t slash = in.path.find_last_of('/');
  in.folder = slash == std::string::npos ? "." : in.path.substr(0, slash);
  in.stem = in.path.substr(slash == std::string::npos ? 0 : slash + 1);
  in.stem = in.stem.substr(0, in.stem.find_last_of('.'));
  const double seconds = args.size() > 1 ? atof(args[1].c_str()) : 20;
  std::string folder = in.folder + "/" + in.stem + "-compare";
  if (!keep)
  {
    const std::string tmp = std::getenv("TEMP") ? std::getenv("TEMP") : "/tmp";
    makeDir(tmp.c_str());
    folder = tmp + "/encoder-compare";
  }
  makeDir(folder.c_str());
  const std::string results = in.folder + "/" + in.stem + "-compare-results.txt";
  out.file.open(results);

  Reader probe;
  const std::string err = probe.open(in.path);
  if (!err.empty())
  {
    out << "Error: " << err << "\n";
    return finish(1);
  }
  in.width = probe.dec->width & ~1;
  in.height = probe.dec->height & ~1;
  const AVRational rate = av_guess_frame_rate(probe.ic, probe.st(), nullptr);
  in.fps = rate.num && rate.den ? float(av_q2d(rate)) : 30.f;
  in.maxRel100ns = int64_t(seconds * 1e7);
  // Keep the recording's own start time so the review app shows the same clock times.
  const AVDictionaryEntry *utc = av_dict_get(probe.ic->metadata, "com.crewtimer.first_utc_us", nullptr, 0);
  const uint64_t utcUs = utc ? std::strtoull(utc->value, nullptr, 10) : 1767225600000000ULL;
  in.firstTs100ns = utcUs * 10;

  out << "encoder-compare: " << in.stem << " " << in.width << "x" << in.height << " " << std::fixed
      << std::setprecision(2) << in.fps << " fps, first " << std::setprecision(0) << seconds
      << " s, FFmpeg " << av_version_info() << "\n";
  if (keep) out << "Videos: " << folder << "\n";
  out << "\n";
  out << "run          encoder                            MB     Mbps  PSNR avg  PSNR min  encode fps\n";
  bool ok = true;
  for (int quality : {60, 70, 80, 90})
    for (bool software : {false, true})
    {
      Run run = encodeRun(in, folder, quality, software);
      if (!keep)
      {
        // encodeRun has closed the files, which Windows requires before deleting them.
        const std::string dir = folder + "/" + run.label;
        std::remove((dir + "/" + in.stem + ".mp4").c_str());
        std::remove((dir + "/tmp-" + in.stem + ".mp4").c_str());
        std::remove((dir + "/" + in.stem + ".json").c_str());
        removeDir(dir.c_str());
      }
      std::ostringstream line;
      line << std::left << std::setw(13) << run.label << std::setw(33) << run.encoder.substr(0, 32);
      if (!run.err.empty())
      {
        line << (run.err.compare(0, 6, "Error:") == 0 ? "" : "Error: ") << run.err;
        ok = false;
      }
      else
      {
        const double duration = run.frames / in.fps;
        line << std::right << std::fixed << std::setprecision(1) << std::setw(7) << run.bytes / 1e6
             << std::setw(9) << run.bytes * 8 / 1e6 / duration << std::setprecision(2) << std::setw(10)
             << run.psnrMean << std::setw(10) << run.psnrMin << std::setprecision(0) << std::setw(12)
             << run.frames / run.encodeSecs;
      }
      out << line.str() << "\n";
    }
  out << "\nHigher PSNR is closer to the input (40+ dB is hard to tell apart).\n";
  if (keep)
    out << "Open each folder in the review app to compare the same frames zoomed in.\n";
  else
    removeDir(folder.c_str());
  std::cout << "Results saved to " << results << "\n";
  return finish(ok ? 0 : 1);
}
