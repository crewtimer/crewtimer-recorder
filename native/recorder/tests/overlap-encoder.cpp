// Risk check for overlapped recording, where one encoder stays open across files and each
// new file starts with the last 2 s of packets:
//   1. Keyframes: forced keyframes land where requested and are IDR.
//   2. SPS/PPS: a file started mid-stream decodes on its own.
//   3. Latency: how many frames the encoder holds before their packets come out.
// Each available encoder runs twice. "today" keeps FFRecorder's settings but leaves the
// encoder open, to show what goes wrong. "proposed" forces keyframes and asks for a global
// header, and must pass. Encoder settings mirror FFVideoRecorder::openVideoStream.
//
// Usage: overlap-encoder [output directory]. Results are also written to
// overlap-encoder-results.txt in the current directory. Build on Windows with
// build-overlap-encoder.bat.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <direct.h>
#define makeDir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define makeDir(path) mkdir(path, 0755)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

static const int W = 1280, H = 720, FPS = 60, FRAMES = 480, GOP = 12;
static const int SPLIT = 307;            // first frame of file B's live part; off the GOP grid
static const int64_t OVERLAP_US = 2000000;
static const int QUALITY = 80;

static int64_t framePts(int i) { return av_rescale(i, 1000000, FPS); }
static int frameOf(int64_t pts) { return int(std::llround(pts * double(FPS) / 1e6)); }

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

static int logErrors = 0;
static std::string mftName;               // h264_mf logs which Media Foundation encoder it picked
static std::vector<std::string> openLog; // warnings and errors while opening an encoder
static void logCallback(void *, int level, const char *fmt, va_list args)
{
  char buf[512];
  vsnprintf(buf, sizeof(buf), fmt, args);
  std::string line = buf;
  line.erase(line.find_last_not_of("\r\n") + 1);
  if (strncmp(fmt, "MFT name", 8) == 0)
    mftName = line;
  else if (level <= AV_LOG_WARNING)
  {
    openLog.push_back(line);
    if (level <= AV_LOG_ERROR)
    {
      ++logErrors;
      std::cerr << line << "\n";
    }
  }
}

// NAL unit types in a packet, Annex B or 4-byte length prefixed.
static std::vector<int> nalTypes(const uint8_t *data, int size)
{
  std::vector<int> types;
  const bool annexB = size > 4 && data[0] == 0 && data[1] == 0 &&
                      (data[2] == 1 || (data[2] == 0 && data[3] == 1));
  if (annexB)
  {
    for (int i = 0; i + 3 < size; ++i)
      if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
      {
        types.push_back(data[i + 3] & 0x1f);
        i += 3;
      }
  }
  else
  {
    for (int i = 0; i + 4 < size;)
    {
      const int len = (data[i] << 24) | (data[i + 1] << 16) | (data[i + 2] << 8) | data[i + 3];
      types.push_back(data[i + 4] & 0x1f);
      i += 4 + len;
    }
  }
  return types;
}
static bool hasNal(const AVPacket *p, int type)
{
  for (int t : nalTypes(p->data, p->size))
    if (t == type) return true;
  return false;
}
static bool hasParameterSets(const AVPacket *p) { return hasNal(p, 7) && hasNal(p, 8); }

// Moving pattern with an abrupt change every 50 frames to provoke scene-cut keyframes.
static void fillFrame(AVFrame *f, int i)
{
  av_frame_make_writable(f);
  const int scene = i / 50;
  uint32_t noise = 12345u + i;
  for (int y = 0; y < H; ++y)
  {
    uint8_t *row = f->data[0] + y * f->linesize[0];
    for (int x = 0; x < W; ++x)
    {
      noise = noise * 1664525u + 1013904223u;
      row[x] = uint8_t(16 + ((x * (scene % 3 + 1) + y * (scene % 5 + 1) + i * 4 + scene * 97) % 200) +
                       ((noise >> 24) & 15));
    }
  }
  const uint8_t u = uint8_t(64 + (scene * 53 + 40) % 128), v = uint8_t(64 + (scene * 53 + 80) % 128);
  if (f->format == AV_PIX_FMT_NV12)
  {
    for (int y = 0; y < H / 2; ++y)
      for (int x = 0; x < W / 2; ++x)
      {
        f->data[1][y * f->linesize[1] + 2 * x] = u;
        f->data[1][y * f->linesize[1] + 2 * x + 1] = v;
      }
  }
  else
  {
    for (int y = 0; y < H / 2; ++y)
    {
      memset(f->data[1] + y * f->linesize[1], u, W / 2);
      memset(f->data[2] + y * f->linesize[2], v, W / 2);
    }
  }
}

struct Variant
{
  const char *codec;
  std::string label;
  bool mfHardware = false; // h264_mf only: hardware MFT, or Microsoft's software encoder
  AVPixelFormat pixFmt = AV_PIX_FMT_YUV420P;
  bool noProfile = false;     // h264_mf: leave the profile at the encoder's default
  bool noRateControl = false; // h264_mf: leave rate control at the encoder's default
  Variant(const char *codec, std::string label, bool mfHardware = false)
      : codec(codec), label(label), mfHardware(mfHardware) {}
};

static AVCodecContext *openEncoder(const AVCodec *codec, const Variant &v, bool proposed, std::string &err)
{
  const std::string name = codec->name;
  const int quantizer = int(std::lround(14 + (100 - QUALITY) * 0.3));
  AVCodecContext *ctx = avcodec_alloc_context3(codec);
  ctx->bit_rate = name == "h264_mf" ? 40000000 : 0;
  ctx->width = W;
  ctx->height = H;
  ctx->framerate = av_make_q(FPS, 1);
  ctx->time_base = av_make_q(1, 1000000);
  ctx->pix_fmt = v.pixFmt;
  ctx->max_b_frames = 0;
  ctx->thread_count = 0;
  ctx->gop_size = GOP;
  if (proposed) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  AVDictionary *opts = nullptr;
  if (name == "libx264")
  {
    av_dict_set(&opts, "preset", "medium", 0);
    av_dict_set_int(&opts, "crf", quantizer, 0);
  }
  else if (name == "h264_nvenc")
  {
    av_dict_set(&opts, "preset", "p4", 0);
    av_dict_set(&opts, "tune", "hq", 0);
    av_dict_set(&opts, "rc", "vbr", 0);
    av_dict_set_int(&opts, "cq", quantizer, 0);
    if (proposed) av_dict_set_int(&opts, "forced-idr", 1, 0);
  }
  else if (name == "h264_qsv")
  {
    av_dict_set(&opts, "preset", "medium", 0);
    ctx->global_quality = quantizer;
  }
  else if (name == "h264_videotoolbox")
  {
    ctx->qmin = -1;
    ctx->qmax = -1;
    ctx->flags |= AV_CODEC_FLAG_QSCALE;
    ctx->global_quality = QUALITY * FF_QP2LAMBDA;
  }
  else if (name == "h264_amf")
  {
    av_dict_set(&opts, "rc", "cqp", 0);
    av_dict_set_int(&opts, "qp_i", quantizer, 0);
    av_dict_set_int(&opts, "qp_p", quantizer, 0);
    av_dict_set_int(&opts, "qp_b", quantizer, 0);
  }
  else if (name == "h264_mf")
  {
    if (!v.noProfile) ctx->profile = AV_PROFILE_H264_HIGH;
    if (!v.noRateControl) av_dict_set(&opts, "rate_control", "u_vbr", 0);
    if (v.mfHardware) av_dict_set_int(&opts, "hw_encoding", 1, 0);
  }
  mftName.clear();
  openLog.clear();
  const int ret = avcodec_open2(ctx, codec, &opts);
  const int unused = av_dict_count(opts);
  av_dict_free(&opts);
  if (ret >= 0 && unused == 0) return ctx;
  avcodec_free_context(&ctx);
  err = ret < 0 ? "could not open" : "options not accepted";
  return nullptr;
}

struct Encoded
{
  std::vector<AVPacket *> packets;
  std::map<int, AVPacket *> byFrame;
  int maxLag = 0;
  int sendRetries = 0;
  std::string err;
};

static bool forcedKey(int i) { return i % GOP == 0 || i == SPLIT; }

static void receive(AVCodecContext *ctx, Encoded &e, int sent)
{
  AVPacket *pkt = av_packet_alloc();
  while (avcodec_receive_packet(ctx, pkt) >= 0)
  {
    const int frame = frameOf(pkt->pts);
    if (sent >= 0) e.maxLag = std::max(e.maxLag, sent - frame);
    e.packets.push_back(av_packet_clone(pkt));
    e.byFrame[frame] = e.packets.back();
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
}

static Encoded encodeAll(AVCodecContext *ctx, bool force)
{
  Encoded e;
  AVFrame *frame = av_frame_alloc();
  frame->format = ctx->pix_fmt;
  frame->width = W;
  frame->height = H;
  frame->color_range = AVCOL_RANGE_MPEG;
  av_frame_get_buffer(frame, 0);
  for (int i = 0; i < FRAMES && e.err.empty(); ++i)
  {
    fillFrame(frame, i);
    frame->pts = framePts(i);
    frame->pict_type = force && forcedKey(i) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    // An async encoder (hardware h264_mf) can refuse input until it has output ready.
    int ret;
    for (int attempt = 0; (ret = avcodec_send_frame(ctx, frame)) == AVERROR(EAGAIN) && attempt < 1000; ++attempt)
    {
      ++e.sendRetries;
      receive(ctx, e, i - 1);
    }
    if (ret < 0)
      e.err = "send_frame failed at frame " + std::to_string(i);
    // Lag counts frames sent after this one before its packet came out.
    receive(ctx, e, i);
  }
  avcodec_send_frame(ctx, nullptr);
  receive(ctx, e, -1);
  av_frame_free(&frame);
  return e;
}

// Write packets[begin, end) to an MP4 with times re-based to the first packet.
static std::string writeMp4(const std::string &path, AVCodecContext *enc,
                            const std::vector<AVPacket *> &packets, size_t begin, size_t end)
{
  AVFormatContext *oc = nullptr;
  avformat_alloc_output_context2(&oc, nullptr, nullptr, path.c_str());
  AVStream *st = avformat_new_stream(oc, nullptr);
  avcodec_parameters_from_context(st->codecpar, enc);
  st->time_base = enc->time_base;
  std::string err;
  if (avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) err = "avio_open failed for " + path;
  else if (avformat_write_header(oc, nullptr) < 0) err = "write_header failed";
  const int64_t base = packets[begin]->pts;
  for (size_t i = begin; err.empty() && i < end; ++i)
  {
    AVPacket *p = av_packet_clone(packets[i]);
    p->pts -= base;
    p->dts -= base;
    p->duration = av_rescale_q(1, av_inv_q(enc->framerate), enc->time_base);
    av_packet_rescale_ts(p, enc->time_base, st->time_base);
    p->stream_index = st->index;
    if (av_write_frame(oc, p) < 0) err = "write_frame failed at packet " + std::to_string(i - begin);
    av_packet_free(&p);
  }
  if (err.empty() && av_write_trailer(oc) < 0) err = "write_trailer failed";
  avio_closep(&oc->pb);
  avformat_free_context(oc);
  return err;
}

struct Decoded
{
  std::vector<int64_t> pts; // microseconds
  std::vector<uint64_t> hashes;
  int errors = 0;
};

static Decoded decodeMp4(const std::string &path)
{
  Decoded d;
  logErrors = 0;
  AVFormatContext *ic = nullptr;
  if (avformat_open_input(&ic, path.c_str(), nullptr, nullptr) < 0 ||
      avformat_find_stream_info(ic, nullptr) < 0)
  {
    d.errors = 1;
    if (ic) avformat_close_input(&ic);
    return d;
  }
  AVStream *st = ic->streams[0];
  const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
  AVCodecContext *dec = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(dec, st->codecpar);
  dec->thread_count = 1;
  avcodec_open2(dec, codec, nullptr);
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  auto drain = [&]()
  {
    while (avcodec_receive_frame(dec, frame) >= 0)
    {
      uint64_t hash = 1469598103934665603ULL;
      for (int y = 0; y < frame->height; ++y)
        for (int x = 0; x < frame->width; ++x)
          hash = (hash ^ frame->data[0][y * frame->linesize[0] + x]) * 1099511628211ULL;
      d.pts.push_back(av_rescale_q(frame->best_effort_timestamp, st->time_base, av_make_q(1, 1000000)));
      d.hashes.push_back(hash);
      av_frame_unref(frame);
    }
  };
  while (av_read_frame(ic, pkt) >= 0)
  {
    if (avcodec_send_packet(dec, pkt) < 0) ++d.errors;
    av_packet_unref(pkt);
    drain();
  }
  avcodec_send_packet(dec, nullptr);
  drain();
  d.errors += logErrors;
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec);
  avformat_close_input(&ic);
  return d;
}

struct Files
{
  std::string err;
  int bStartFrame = -1;
  Decoded a, b;
};

static std::string fileTag(const Variant &v, const char *mode)
{
  std::string tag = std::string(v.label) + "-" + mode;
  std::replace(tag.begin(), tag.end(), ' ', '-');
  return tag;
}

// File A holds frames before SPLIT. File B starts at the latest keyframe at least
// OVERLAP_US before SPLIT and runs to the end.
static Files splitFiles(const std::string &dir, const std::string &tag, AVCodecContext *enc,
                        const Encoded &e)
{
  Files f;
  const int64_t splitPts = framePts(SPLIT);
  size_t aEnd = 0, bBegin = e.packets.size();
  for (size_t i = 0; i < e.packets.size(); ++i)
  {
    if (e.packets[i]->pts < splitPts) aEnd = i + 1;
    if ((e.packets[i]->flags & AV_PKT_FLAG_KEY) && e.packets[i]->pts <= splitPts - OVERLAP_US)
      bBegin = i;
  }
  if (bBegin == e.packets.size())
  {
    f.err = "no keyframe 2 s before the split";
    return f;
  }
  f.bStartFrame = frameOf(e.packets[bBegin]->pts);
  const std::string a = dir + "/" + tag + "-A.mp4", b = dir + "/" + tag + "-B.mp4";
  f.err = writeMp4(a, enc, e.packets, 0, aEnd);
  if (f.err.empty()) f.err = writeMp4(b, enc, e.packets, bBegin, e.packets.size());
  if (!f.err.empty()) return f;
  f.a = decodeMp4(a);
  f.b = decodeMp4(b);
  return f;
}

static void freePackets(Encoded &e)
{
  for (AVPacket *p : e.packets) av_packet_free(&p);
  e.packets.clear();
}

// Encoder kept open with today's settings: report where it breaks.
static void runToday(const AVCodec *codec, const Variant &v, const std::string &dir)
{
  std::string err;
  AVCodecContext *enc = openEncoder(codec, v, false, err);
  if (!enc)
  {
    out << "  today:    skipped (" << err << ")\n";
    return;
  }
  Encoded e = encodeAll(enc, false);
  int gridNotKey = 0, extraKeys = 0, nonIdrKeys = 0, keysNoSps = 0, dtsDiffers = 0;
  for (auto &kv : e.byFrame)
  {
    const AVPacket *p = kv.second;
    const bool key = p->flags & AV_PKT_FLAG_KEY;
    if (kv.first % GOP == 0 && !key) ++gridNotKey;
    if (kv.first % GOP != 0 && key) ++extraKeys;
    if (key && !hasNal(p, 5)) ++nonIdrKeys;
    if (key && kv.first > 0 && !hasParameterSets(p)) ++keysNoSps;
    if (p->dts != p->pts) ++dtsDiffers;
  }
  out << "  today:    grid frames not key=" << gridNotKey << "/" << (FRAMES + GOP - 1) / GOP
      << " extra keys=" << extraKeys << " non-IDR keys=" << nonIdrKeys
      << " later keys without SPS/PPS=" << keysNoSps << " dts!=pts=" << dtsDiffers
      << " lag=" << e.maxLag << " frames\n";
  if (!e.err.empty())
    out << "            " << e.err << "\n";
  Files f = splitFiles(dir, fileTag(v, "today"), enc, e);
  if (!f.err.empty())
    out << "            file B: " << f.err << "\n";
  else
    out << "            file B from frame " << f.bStartFrame << ": decoded "
        << f.b.hashes.size() << "/" << FRAMES - f.bStartFrame
        << " frames, " << f.b.errors << " decode errors\n";
  freePackets(e);
  avcodec_free_context(&enc);
}

// Forced keyframes and a global header: every check must pass.
static bool runProposed(const AVCodec *codec, const Variant &v, const std::string &dir)
{
  std::string err;
  AVCodecContext *enc = openEncoder(codec, v, true, err);
  if (!enc)
  {
    out << "  proposed: FAIL (" << err << ")\n";
    return false;
  }
  std::vector<std::string> failures;
  Encoded e = encodeAll(enc, true);
  if (!e.err.empty()) failures.push_back(e.err);
  if (int(e.byFrame.size()) != FRAMES)
    failures.push_back("packets for " + std::to_string(e.byFrame.size()) + "/" + std::to_string(FRAMES) + " frames");
  int missedForced = 0, extraKeys = 0, forcedNoSps = 0;
  for (auto &kv : e.byFrame)
  {
    const bool key = kv.second->flags & AV_PKT_FLAG_KEY;
    if (forcedKey(kv.first) && !(key && hasNal(kv.second, 5))) ++missedForced;
    if (forcedKey(kv.first) && !hasParameterSets(kv.second)) ++forcedNoSps;
    if (!forcedKey(kv.first) && key) ++extraKeys;
  }
  if (missedForced) failures.push_back(std::to_string(missedForced) + " forced frames not IDR");
  // Some MFTs give no global header and only carry SPS/PPS inside keyframes. The muxer then
  // takes them from the file's first packet, so either source works.
  std::string paramSets;
  if (enc->extradata_size > 0)
    paramSets = "global header " + std::to_string(enc->extradata_size) + " bytes";
  else if (forcedNoSps == 0)
    paramSets = "in keyframes only (no global header)";
  else
  {
    paramSets = "missing";
    failures.push_back("no global header and " + std::to_string(forcedNoSps) + " keyframes lack SPS/PPS");
  }

  Files f = splitFiles(dir, fileTag(v, "proposed"), enc, e);
  if (!f.err.empty())
    failures.push_back(f.err);
  else
  {
    const size_t bFrames = FRAMES - f.bStartFrame;
    if (f.a.hashes.size() != SPLIT || f.a.errors)
      failures.push_back("file A decoded " + std::to_string(f.a.hashes.size()) + "/" + std::to_string(SPLIT) +
                         " frames, " + std::to_string(f.a.errors) + " errors");
    if (f.b.hashes.size() != bFrames || f.b.errors)
      failures.push_back("file B decoded " + std::to_string(f.b.hashes.size()) + "/" + std::to_string(bFrames) +
                         " frames, " + std::to_string(f.b.errors) + " errors");
    if (!f.b.pts.empty() && f.b.pts[0] != 0)
      failures.push_back("file B starts at " + std::to_string(f.b.pts[0]) + " us");
    // Overlap frames must decode to the same picture in both files.
    int mismatched = 0;
    for (size_t i = 0; i < f.b.hashes.size() && f.bStartFrame + i < f.a.hashes.size(); ++i)
      if (f.b.hashes[i] != f.a.hashes[f.bStartFrame + i]) ++mismatched;
    if (mismatched) failures.push_back(std::to_string(mismatched) + " overlap frames differ");
  }
  out << "  proposed: " << (failures.empty() ? "PASS" : "FAIL") << " SPS/PPS " << paramSets
      << ", extra keys=" << extraKeys << ", lag=" << e.maxLag << " frames (" << e.maxLag * 1000 / FPS << " ms)";
  if (f.err.empty() && f.bStartFrame >= 0)
    out << ", overlap=" << SPLIT - f.bStartFrame << " frames";
  if (e.sendRetries) out << ", send retries=" << e.sendRetries;
  out << "\n";
  for (auto &msg : failures) out << "            " << msg << "\n";
  freePackets(e);
  avcodec_free_context(&enc);
  return failures.empty();
}

int main(int argc, char **argv)
{
  std::string dir;
  if (argc > 1)
    dir = argv[1];
  else
  {
    const char *tmp = std::getenv("TEMP");
    dir = std::string(tmp ? tmp : "/tmp") + "/overlap-encoder";
    makeDir(dir.c_str());
  }
  out.file.open("overlap-encoder-results.txt");
  av_log_set_level(AV_LOG_ERROR);
  av_log_set_callback(logCallback);
  out << "overlap-encoder: FFmpeg " << av_version_info() << ", " << W << "x" << H << " " << FPS
      << " fps, split at frame " << SPLIT << ", 2 s overlap\n";

  // A hardware MFT that rejects the recorder's settings is retried with NV12 input (often
  // the only format GPU encoders take) and fewer options, to find which setting it rejects.
  std::vector<std::vector<Variant>> variants = {
      {Variant("h264_videotoolbox", "h264_videotoolbox")},
      {Variant("h264_nvenc", "h264_nvenc")},
      {Variant("h264_qsv", "h264_qsv")},
      {Variant("h264_amf", "h264_amf")},
      {},
      {Variant("h264_mf", "h264_mf software")},
      {Variant("libx264", "libx264")},
  };
  for (int i = 0; i < 5; ++i)
  {
    static const char *labels[] = {"recorder settings", "NV12", "NV12, default profile",
                                   "NV12, default rate control", "NV12, default profile and rate control"};
    Variant v("h264_mf", std::string("h264_mf hardware, ") + labels[i], true);
    v.pixFmt = i == 0 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
    v.noProfile = i == 2 || i == 4;
    v.noRateControl = i == 3 || i == 4;
    variants[4].push_back(v);
  }
  bool ok = true;
  int tested = 0;
  for (const std::vector<Variant> &attempts : variants)
  {
    const AVCodec *codec = attempts.empty() ? nullptr : avcodec_find_encoder_by_name(attempts[0].codec);
    if (!codec) continue;
    const Variant *chosen = nullptr;
    for (const Variant &attempt : attempts)
    {
      std::string err;
      AVCodecContext *probe = openEncoder(codec, attempt, false, err);
      if (probe)
      {
        avcodec_free_context(&probe);
        chosen = &attempt;
        break;
      }
      out << attempt.label << ": not usable on this machine (" << err << ")\n";
      for (const std::string &line : openLog) out << "    ffmpeg: " << line << "\n";
    }
    if (!chosen) continue;
    const Variant &v = *chosen;
    out << v.label << (mftName.empty() ? "" : " (" + mftName + ")") << ":\n";
    runToday(codec, v, dir);
    ok = runProposed(codec, v, dir) && ok;
    ++tested;
  }
  if (!tested) out << "No H.264 encoder available\n";
  out << (ok && tested ? "RESULT: PASS" : "RESULT: FAIL") << "\n";
  if (argc < 2)
  {
    // Started without arguments, e.g. by double-click: keep the window open.
    std::cout << "Results saved to overlap-encoder-results.txt. Press Enter to exit.";
    std::cin.get();
  }
  return ok && tested ? 0 : 1;
}
