// Risk check for overlapped recording, where one encoder stays open across files and each
// new file starts with the last 2 s of packets:
//   1. Keyframes: forced keyframes land where requested and are IDR.
//   2. SPS/PPS: a file started mid-stream decodes on its own.
//   3. Latency: how many frames the encoder holds before their packets come out.
// Each available encoder runs twice. "today" keeps FFRecorder's settings but leaves the
// encoder open, to show what goes wrong. "proposed" forces keyframes and uses a global
// header, and must pass. Encoder settings mirror FFVideoRecorder::openVideoStream.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
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

static int logErrors = 0;
static void countErrors(void *, int level, const char *fmt, va_list args)
{
  if (level <= AV_LOG_ERROR)
  {
    ++logErrors;
    vfprintf(stderr, fmt, args);
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
  for (int p = 1; p < 3; ++p)
    for (int y = 0; y < H / 2; ++y)
      memset(f->data[p] + y * f->linesize[p], 64 + (scene * 53 + p * 40) % 128, W / 2);
}

static AVCodecContext *openEncoder(const AVCodec *codec, bool proposed, std::string &err)
{
  const std::string name = codec->name;
  const int quantizer = int(std::lround(14 + (100 - QUALITY) * 0.3));
  for (int attempt = 0; attempt < 2; ++attempt)
  {
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    ctx->bit_rate = name == "h264_mf" ? 40000000 : 0;
    ctx->width = W;
    ctx->height = H;
    ctx->framerate = av_make_q(FPS, 1);
    ctx->time_base = av_make_q(1, 1000000);
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
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
      if (proposed) av_dict_set_int(&opts, "forced_idr", 1, 0);
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
      ctx->profile = AV_PROFILE_H264_HIGH;
      av_dict_set(&opts, "rate_control", "u_vbr", 0);
      if (attempt == 0) av_dict_set_int(&opts, "hw_encoding", 1, 0);
    }
    const int ret = avcodec_open2(ctx, codec, &opts);
    const int unused = av_dict_count(opts);
    av_dict_free(&opts);
    if (ret >= 0 && unused == 0) return ctx;
    avcodec_free_context(&ctx);
    err = ret < 0 ? "could not open" : "options not accepted";
    if (name != "h264_mf") break; // only h264_mf has a software fallback
  }
  return nullptr;
}

struct Encoded
{
  std::vector<AVPacket *> packets;
  std::map<int, AVPacket *> byFrame;
  int maxLag = 0;
  std::string err;
};

static bool forcedKey(int i) { return i % GOP == 0 || i == SPLIT; }

static void receive(AVCodecContext *ctx, Encoded &out, int sent)
{
  AVPacket *pkt = av_packet_alloc();
  while (avcodec_receive_packet(ctx, pkt) >= 0)
  {
    const int frame = frameOf(pkt->pts);
    if (sent >= 0) out.maxLag = std::max(out.maxLag, sent - frame);
    out.packets.push_back(av_packet_clone(pkt));
    out.byFrame[frame] = out.packets.back();
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
}

static Encoded encodeAll(AVCodecContext *ctx, bool force)
{
  Encoded out;
  AVFrame *frame = av_frame_alloc();
  frame->format = ctx->pix_fmt;
  frame->width = W;
  frame->height = H;
  frame->color_range = AVCOL_RANGE_MPEG;
  av_frame_get_buffer(frame, 0);
  for (int i = 0; i < FRAMES; ++i)
  {
    fillFrame(frame, i);
    frame->pts = framePts(i);
    frame->pict_type = force && forcedKey(i) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    if (avcodec_send_frame(ctx, frame) < 0)
    {
      out.err = "send_frame failed at frame " + std::to_string(i);
      break;
    }
    // Lag counts frames sent after this one before its packet came out.
    receive(ctx, out, i);
  }
  avcodec_send_frame(ctx, nullptr);
  receive(ctx, out, -1);
  av_frame_free(&frame);
  return out;
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
  if (avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) err = "avio_open failed";
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
  std::vector<int64_t> pts; // stream time base converted to microseconds
  std::vector<uint64_t> hashes;
  int errors = 0;
};

static Decoded decodeMp4(const std::string &path)
{
  Decoded out;
  logErrors = 0;
  AVFormatContext *ic = nullptr;
  if (avformat_open_input(&ic, path.c_str(), nullptr, nullptr) < 0 ||
      avformat_find_stream_info(ic, nullptr) < 0)
  {
    out.errors = 1;
    if (ic) avformat_close_input(&ic);
    return out;
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
      out.pts.push_back(av_rescale_q(frame->best_effort_timestamp, st->time_base, av_make_q(1, 1000000)));
      out.hashes.push_back(hash);
      av_frame_unref(frame);
    }
  };
  while (av_read_frame(ic, pkt) >= 0)
  {
    if (avcodec_send_packet(dec, pkt) < 0) ++out.errors;
    av_packet_unref(pkt);
    drain();
  }
  avcodec_send_packet(dec, nullptr);
  drain();
  out.errors += logErrors;
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec);
  avformat_close_input(&ic);
  return out;
}

struct Files
{
  std::string err;
  int bStartFrame = -1;
  Decoded a, b;
};

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
static void runToday(const AVCodec *codec, const std::string &dir)
{
  std::string err;
  AVCodecContext *enc = openEncoder(codec, false, err);
  if (!enc)
  {
    std::cout << "  today:    skipped (" << err << ")\n";
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
    if (key && kv.first > 0 && !(hasNal(p, 7) && hasNal(p, 8))) ++keysNoSps;
    if (p->dts != p->pts) ++dtsDiffers;
  }
  Files f = splitFiles(dir, std::string(codec->name) + "-today", enc, e);
  std::cout << "  today:    grid frames not key=" << gridNotKey << "/" << (FRAMES + GOP - 1) / GOP
            << " extra keys=" << extraKeys << " non-IDR keys=" << nonIdrKeys
            << " later keys without SPS/PPS=" << keysNoSps << " dts!=pts=" << dtsDiffers
            << " lag=" << e.maxLag << " frames\n";
  if (!f.err.empty())
    std::cout << "            file B: " << f.err << "\n";
  else
    std::cout << "            file B from frame " << f.bStartFrame << ": decoded "
              << f.b.hashes.size() << "/" << FRAMES - f.bStartFrame
              << " frames, " << f.b.errors << " decode errors\n";
  freePackets(e);
  avcodec_free_context(&enc);
}

// Forced keyframes and a global header: every check must pass.
static bool runProposed(const AVCodec *codec, const std::string &dir)
{
  std::string err;
  AVCodecContext *enc = openEncoder(codec, true, err);
  if (!enc)
  {
    std::cout << "  proposed: FAIL (" << err << ")\n";
    return false;
  }
  std::vector<std::string> failures;
  if (enc->extradata_size == 0) failures.push_back("no extradata from global header");
  Encoded e = encodeAll(enc, true);
  if (!e.err.empty()) failures.push_back(e.err);
  if (int(e.byFrame.size()) != FRAMES)
    failures.push_back("packets for " + std::to_string(e.byFrame.size()) + "/" + std::to_string(FRAMES) + " frames");
  int missedForced = 0, extraKeys = 0;
  for (auto &kv : e.byFrame)
  {
    const bool idr = (kv.second->flags & AV_PKT_FLAG_KEY) && hasNal(kv.second, 5);
    if (forcedKey(kv.first) && !idr) ++missedForced;
    if (!forcedKey(kv.first) && (kv.second->flags & AV_PKT_FLAG_KEY)) ++extraKeys;
  }
  if (missedForced) failures.push_back(std::to_string(missedForced) + " forced frames not IDR");

  Files f = splitFiles(dir, std::string(codec->name) + "-proposed", enc, e);
  int mismatched = 0;
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
    for (size_t i = 0; i < f.b.hashes.size() && f.bStartFrame + int(i) < int(f.a.hashes.size()); ++i)
      if (f.b.hashes[i] != f.a.hashes[f.bStartFrame + i]) ++mismatched;
    if (mismatched) failures.push_back(std::to_string(mismatched) + " overlap frames differ");
  }
  std::cout << "  proposed: " << (failures.empty() ? "PASS" : "FAIL")
            << " extradata=" << enc->extradata_size << " bytes, extra keys=" << extraKeys
            << ", lag=" << e.maxLag << " frames (" << e.maxLag * 1000 / FPS << " ms)";
  if (f.err.empty() && f.bStartFrame >= 0)
    std::cout << ", overlap=" << SPLIT - f.bStartFrame << " frames";
  std::cout << "\n";
  for (auto &msg : failures) std::cout << "            " << msg << "\n";
  freePackets(e);
  avcodec_free_context(&enc);
  return failures.empty();
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    std::cerr << "usage: overlap-encoder <output directory>\n";
    return 2;
  }
  av_log_set_level(AV_LOG_ERROR);
  av_log_set_callback(countErrors);
  bool ok = true;
  int tested = 0;
  for (const char *name : {"h264_videotoolbox", "h264_nvenc", "h264_qsv", "h264_amf", "h264_mf", "libx264"})
  {
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (!codec) continue;
    std::string err;
    AVCodecContext *probe = openEncoder(codec, false, err);
    if (!probe)
    {
      std::cout << name << ": not usable on this machine (" << err << ")\n";
      continue;
    }
    avcodec_free_context(&probe);
    std::cout << name << ":\n";
    runToday(codec, argv[1]);
    ok = runProposed(codec, argv[1]) && ok;
    ++tested;
  }
  if (!tested)
  {
    std::cout << "No H.264 encoder available\n";
    return 1;
  }
  return ok ? 0 : 1;
}
