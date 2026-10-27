#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "SystemEventQueue.hpp"
#include "TimingConstants.hpp"
#include "VideoRecorder.hpp"

// Map recording quality to a Media Foundation bitrate: Standard (60)=20, Medium (70)=30,
// High (80)=40, Very High (90)=60 Mbps, interpolated between presets and clamped outside.
static int64_t mediaFoundationBitRate(int quality)
{
  static const int points[][2] = {{60, 20}, {70, 30}, {80, 40}, {90, 60}};
  const int count = sizeof(points) / sizeof(points[0]);
  if (quality <= points[0][0])
  {
    return points[0][1] * 1000000LL;
  }
  for (int i = 1; i < count; ++i)
  {
    if (quality <= points[i][0])
    {
      const double t = double(quality - points[i - 1][0]) / (points[i][0] - points[i - 1][0]);
      return std::llround((points[i - 1][1] + t * (points[i][1] - points[i - 1][1])) * 1000000);
    }
  }
  return points[count - 1][1] * 1000000LL;
}

// One encoder runs from openVideoStream to stop. Without overlap it writes a single file.
// With overlap, splitVideoStream starts further files while the encoder keeps running:
// each new file begins with the encoder's recent packets from a forced keyframe at least
// RecordingOverlap100ns back, and the previous file closes once the encoder has produced
// its last packet. Packet times are relative to the first frame and re-based per file.
class FFVideoRecorder : public VideoRecorder
{
  struct OutputFile
  {
    RecordedFile info;
    std::string tmpPath;
    AVFormatContext *formatCtx = nullptr;
    AVStream *stream = nullptr;
    // A file after the first opens at its first keyframe at or after startPts.
    bool started = false;
    // Its MP4 could not be created; the previous file keeps every packet instead.
    bool failed = false;
    int64_t startPts = 0;
    // Packets from endPts on belong to the next file.
    int64_t endPts = INT64_MAX;
    // First frame not in the previous file. The next file's overlap stops here, so only
    // neighbouring files overlap.
    int64_t livePts = 0;
  };
  // An encoded packet kept for the overlap, with its frame's capture time.
  struct BufferedPacket
  {
    AVPacket *packet;
    uint64_t ts100ns;
  };

  uint64_t firstTimestamp100ns = 0; // capture time of the first frame; pts are relative to it
  int64_t lastFramePts = AV_NOPTS_VALUE;
  int64_t framesSent = 0;
  bool forceKeyFrame = false;
  AVFrame *pFrame = nullptr;
  AVPacket *pkt = nullptr;
  struct SwsContext *sws_ctx = nullptr;
  AVCodecContext *pCodecCtx = nullptr;
  // Oldest first; more than one while earlier files wait for their last packets.
  std::deque<OutputFile> outputs;
  // Overlap only: recent packets, forced keyframes, and the capture times of recent frames.
  std::deque<BufferedPacket> history;
  std::deque<int64_t> keyFramePts;
  std::deque<std::pair<int64_t, uint64_t>> sentFrames;
  std::string codecName;
  const int recordingQuality;
  const bool forceSoftware;
  const bool overlap;

public:
  FFVideoRecorder(int quality, bool forceSoftware, bool overlap)
      : recordingQuality(quality), forceSoftware(forceSoftware), overlap(overlap)
  {
    // Get the version of the libavutil library
    unsigned version = avutil_version();

    std::stringstream ss;
    ss << "version " << av_version_info() << " " << AV_VERSION_MAJOR(version)
       << "." << AV_VERSION_MINOR(version) << "." << AV_VERSION_MICRO(version);
    SystemEventQueue::push("ffmpeg", ss.str());
  }

  int getKeyFrameInterval()
  {
    return 12;
  }

  static std::string iso8601_utc_now_us(uint64_t us)
  {
    // using namespace std::chrono;
    // auto now = time_point_cast<milliseconds>(system_clock::now());
    // auto ms = now.time_since_epoch().count();
    std::time_t secs = us / 1000000;
    int usec = static_cast<int>(us % 1000000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &secs);
#else
    gmtime_r(&secs, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, usec);
    return std::string(buf);
  }

  // Return Unix epoch microseconds (UTC) as int64
  static long long unix_epoch_us_now()
  {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
  }
  // Also make milliseconds for backward compatibility
  static long long unix_epoch_ms_now()
  {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  }

  static bool muxer_has_opt(AVFormatContext *oc, const char *opt_name)
  {
    if (!oc || !oc->priv_data)
      return false;
    const AVOption *o = av_opt_find(oc->priv_data, opt_name, nullptr, 0, 0);
    return o != nullptr;
  }
  std::string openVideoStream(std::string directory, std::string filename,
                              int width, int height, float fps, uint64_t timestamp)
  {
    // A bad source frame rate estimate must not reach the encoder, whose rate
    // control budgets bits per frame from it.
    constexpr float maxEncodeFps = 120.0f;
    if (fps > maxEncodeFps)
    {
      SystemEventQueue::push("ffmpeg", "Warning: source frame rate " + std::to_string(fps) +
                                           " fps exceeds encoder limit; encoding at " +
                                           std::to_string(maxEncodeFps) + " fps");
      fps = maxEncodeFps;
    }
    firstTimestamp100ns = timestamp;
    lastFramePts = AV_NOPTS_VALUE;
    framesSent = 0;
    forceKeyFrame = false;

    av_log_set_level(AV_LOG_ERROR);
    // av_log_set_level(AV_LOG_INFO);

    // Codec names in hardware accelerated preferred order, platform-specific
#if defined(__APPLE__)
    std::vector<std::string> codec_names = {
#if defined(__aarch64__) || defined(__arm64__)
        "h264_videotoolbox", // Constant quality requires Apple Silicon
#endif
        "libx264",
        "libx264rgb"};
#elif defined(_WIN32)
    std::vector<std::string> codec_names = {
        "h264_nvenc", // NVIDIA
        "h264_qsv",   // Intel Quick Sync
        "h264_amf",   // AMD
        "h264_mf",    // Windows Media Foundation (HW on Win10+)
        "libx264",
        "libx264rgb"};
#else
    std::vector<std::string> codec_names = {
        "h264_nvenc",   // NVIDIA on Linux
        "h264_qsv",     // Intel on Linux
        "libx264",
        "libx264rgb"};
#endif

    const AVCodec *codec = nullptr;
    for (const auto &name : codec_names)
    {
      // h264_mf stays: it has a software mode.
      if (forceSoftware && name != "libx264" && name != "libx264rgb" && name != "h264_mf")
      {
        continue;
      }
      codec = avcodec_find_encoder_by_name(
          name.c_str()); // Convert std::string to const char*
      if (codec)
      {
        break;
      }
    }

    if (!codec)
    {
      auto msg = "Error: No H.264 encoder supporting recording quality is available";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }

    if (codecName.empty())
    {
      codecName = codec->name;
      SystemEventQueue::push("ffmpeg", "Using codec " + codecName);
    }

    // Media Foundation targets a bitrate; other encoders target quality.
    const int64_t mfBitRate = mediaFoundationBitRate(recordingQuality);
    // Hardware MFTs (Intel Quick Sync at least) only accept NV12 and fail format
    // negotiation on YUV420P. The software fallback keeps YUV420P.
    bool mfHardware = codecName == "h264_mf" && !forceSoftware;
    auto allocCodecContext = [&]() -> AVCodecContext *
    {
      AVCodecContext *ctx = avcodec_alloc_context3(codec);
      if (!ctx)
      {
        return nullptr;
      }
      // Quality controls allocate bitrate according to scene complexity.
      ctx->bit_rate = codecName == "h264_mf" ? mfBitRate : 0;
      ctx->width = width;
      ctx->height = height;
      ctx->framerate = av_d2q(fps, 100000);
      // Preserve source timing independently of the nominal frame rate.
      ctx->time_base = av_make_q(1, 1000000);
      ctx->pix_fmt = mfHardware ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
      ctx->max_b_frames = 0;
      ctx->thread_count = 0; // let codec decide
      ctx->gop_size = getKeyFrameInterval();
      if (overlap)
      {
        // A file started mid-stream needs SPS/PPS from the codec, not its first packet.
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
      }
      if (std::string("h264_videotoolbox") == codec->name)
      {
        ctx->qmin = -1;
        ctx->qmax = -1;
      }
      if (codecName == "h264_mf")
      {
        // h264_mf defaults to Constrained Baseline, which disables CABAC and 8x8 transforms.
        ctx->profile = AV_PROFILE_H264_HIGH;
      }
      return ctx;
    };

    pCodecCtx = allocCodecContext();
    if (!pCodecCtx)
    {
      auto msg = "Error: Could not allocate video codec context";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }

    // Map the app scale to CRF/CQ/ICQ: Standard=26, Medium=23, High=20, Very High=17.
    // Encoder scales are approximate equivalents, not identical visual quality.
    const int quantizer = static_cast<int>(std::lround(14 + (100 - recordingQuality) * 0.3));
    AVDictionary *codec_options = NULL;
    if (codecName == "libx264" || codecName == "libx264rgb")
    {
      // 'preset' controls compression speed; slower = more compression
      av_dict_set(&codec_options, "preset", "medium", 0);
      av_dict_set_int(&codec_options, "crf", quantizer, 0);
    }
    else if (codecName == "h264_nvenc")
    {
      av_dict_set(&codec_options, "preset", "p4", 0); // balanced quality/speed
      av_dict_set(&codec_options, "tune", "hq", 0);
      av_dict_set(&codec_options, "rc", "vbr", 0);
      av_dict_set_int(&codec_options, "cq", quantizer, 0);
      if (overlap)
      {
        // Forced keyframes must be IDR to start a file.
        av_dict_set_int(&codec_options, "forced_idr", 1, 0);
      }
    }
    else if (codecName == "h264_qsv")
    {
      av_dict_set(&codec_options, "preset", "medium", 0);
      // QSV selects ICQ when global_quality > 0 and no bitrate cap is set.
      pCodecCtx->global_quality = quantizer;
    }
    else if (codecName == "h264_videotoolbox")
    {
      pCodecCtx->flags |= AV_CODEC_FLAG_QSCALE;
      pCodecCtx->global_quality = recordingQuality * FF_QP2LAMBDA;
    }
    else if (codecName == "h264_amf")
    {
      av_dict_set(&codec_options, "rc", "cqp", 0);
      av_dict_set_int(&codec_options, "qp_i", quantizer, 0);
      av_dict_set_int(&codec_options, "qp_p", quantizer, 0);
      av_dict_set_int(&codec_options, "qp_b", quantizer, 0);
    }
    else if (codecName == "h264_mf")
    {
      // Hardware MFTs implement quality mode inconsistently, so target a bitrate.
      av_dict_set(&codec_options, "rate_control", "u_vbr", 0);
      if (mfHardware)
      {
        av_dict_set_int(&codec_options, "hw_encoding", 1, 0);
      }
    }
    else
    {
      const auto msg = "Error: Encoder does not support recording quality: " + codecName;
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }

    int openResult = avcodec_open2(pCodecCtx, codec, &codec_options);
    if (openResult < 0 && mfHardware)
    {
      // No usable hardware MFT; fall back to Microsoft's software encoder.
      SystemEventQueue::push("ffmpeg", "Warning: Hardware h264_mf unavailable, using software encoder");
      av_dict_free(&codec_options);
      avcodec_free_context(&pCodecCtx);
      mfHardware = false;
      pCodecCtx = allocCodecContext();
      if (!pCodecCtx)
      {
        auto msg = "Error: Could not allocate video codec context";
        SystemEventQueue::push("ffmpeg", msg);
        return msg;
      }
      av_dict_set(&codec_options, "rate_control", "u_vbr", 0);
      openResult = avcodec_open2(pCodecCtx, codec, &codec_options);
    }
    if (openResult < 0)
    {
      auto msg = "Error: Could not open codec " + codecName;
      SystemEventQueue::push("ffmpeg", msg);
      av_dict_free(&codec_options);
      return msg;
    }

    if (av_dict_count(codec_options) != 0)
    {
      const auto msg = "Error: Recording quality options were not accepted by " + codecName;
      av_dict_free(&codec_options);
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    av_dict_free(&codec_options);
    SystemEventQueue::push("ffmpeg", "Recording quality " + std::to_string(recordingQuality) +
                                    " using " + codecName +
                                    (codecName == "h264_mf"
                                         ? std::string(mfHardware ? " hardware" : " software") + " at " +
                                               std::to_string(mfBitRate / 1000000) + " Mbps"
                                         : ""));

    pFrame = av_frame_alloc();
    if (!pFrame)
    {
      auto msg = "Error: Could not allocate video frame";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    pFrame->format = pCodecCtx->pix_fmt;
    pFrame->width = pCodecCtx->width;
    pFrame->height = pCodecCtx->height;
    pFrame->color_range = AVCOL_RANGE_MPEG;
    if (av_frame_get_buffer(pFrame, 0) < 0)
    {
      auto msg = "Error: Could not allocate the video frame data";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }

    pkt = av_packet_alloc();
    if (!pkt)
    {
      auto msg = "Error: Unable to allocate AVPacket";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    pkt->data = NULL;
    pkt->size = 0;
    sws_ctx = nullptr;

    // The first file starts with the encoder's first packet, which is always a keyframe.
    OutputFile first;
    first.info.directory = directory;
    first.info.filename = filename;
    outputs.push_back(first);
    return openMuxer(outputs.back());
  }

  std::string writeVideoFrame(FramePtr video_frame)
  {
    // The encoder may still reference the previous frame's buffer.
    if (av_frame_make_writable(pFrame) < 0)
    {
      const std::string msg = "Error: Could not allocate the video frame data";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    if (video_frame->pixelFormat == Frame::PixelFormat::YUV420P)
    {
      // I420 layout: Y plane at data, U at data+w*h, V at data+w*h*5/4
      // Direct plane copy into the encoder AVFrame — no color space conversion needed
      const int w = pCodecCtx->width;
      const int h = pCodecCtx->height;
      const uint8_t *srcY = video_frame->data;
      const uint8_t *srcU = srcY + w * h;
      const uint8_t *srcV = srcU + (w / 2) * (h / 2);
      for (int y = 0; y < h; y++)
        memcpy(pFrame->data[0] + y * pFrame->linesize[0], srcY + y * w, w);
      if (pCodecCtx->pix_fmt == AV_PIX_FMT_NV12)
      {
        // NV12 interleaves the chroma planes: U V U V ...
        for (int y = 0; y < h / 2; y++)
        {
          uint8_t *dst = pFrame->data[1] + y * pFrame->linesize[1];
          const uint8_t *u = srcU + y * (w / 2);
          const uint8_t *v = srcV + y * (w / 2);
          for (int x = 0; x < w / 2; x++)
          {
            dst[2 * x] = u[x];
            dst[2 * x + 1] = v[x];
          }
        }
      }
      else
      {
        for (int y = 0; y < h / 2; y++)
          memcpy(pFrame->data[1] + y * pFrame->linesize[1], srcU + y * (w / 2), w / 2);
        for (int y = 0; y < h / 2; y++)
          memcpy(pFrame->data[2] + y * pFrame->linesize[2], srcV + y * (w / 2), w / 2);
      }
    }
    else
    {
      int inLinesize[1] = {video_frame->stride};
      if (sws_ctx == nullptr)
      {
        auto src_fmt = AV_PIX_FMT_UYVY422;
        switch (video_frame->pixelFormat)
        {
        case Frame::PixelFormat::BGR:
          src_fmt = AV_PIX_FMT_BGR24;
          inLinesize[0] = {3 * video_frame->xres};
          break;
        case Frame::PixelFormat::UYVY422:
        default:
          src_fmt = AV_PIX_FMT_UYVY422;
          break;
        }
        sws_ctx = sws_getContext(
            pCodecCtx->width, pCodecCtx->height, src_fmt, pCodecCtx->width,
            pCodecCtx->height, pCodecCtx->pix_fmt,
            SWS_BICUBIC, NULL, NULL, NULL);
      }
      uint8_t *inData[1] = {video_frame->data};
      sws_scale(sws_ctx, inData, inLinesize, 0, pCodecCtx->height, pFrame->data,
                pFrame->linesize);
    }

    // Both NDI and SRT supply corrected capture times in 100 ns units.
    // Start each recording at zero, preserving gaps in the source timeline.
    if (video_frame->timestamp < firstTimestamp100ns)
    {
      const std::string msg = "Error: Frame timestamp precedes recording start";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    const int64_t pts = toPts(video_frame->timestamp);
    if (lastFramePts != AV_NOPTS_VALUE && pts <= lastFramePts)
    {
      const std::string msg = "Error: Recording frame timestamps must increase";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    pFrame->pts = pts;
    // Overlapped files start at forced keyframes, so their positions are known before the
    // encoder produces them.
    const bool keyFrame = overlap && (framesSent % getKeyFrameInterval() == 0 || forceKeyFrame);
    pFrame->pict_type = keyFrame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    std::string errorMsg = "";
    if (avcodec_send_frame(pCodecCtx, pFrame) < 0)
    {
      errorMsg = "Error: Cannot send a frame for encoding";
      SystemEventQueue::push("ffmpeg", errorMsg);
      // Do not return yet; still drain packets below
    }
    else
    {
      lastFramePts = pts;
      ++framesSent;
      forceKeyFrame = false;
      if (overlap)
      {
        sentFrames.push_back(std::make_pair(pts, video_frame->timestamp));
        if (keyFrame)
        {
          keyFramePts.push_back(pts);
        }
      }
    }

    while (1)
    {
      int ret = avcodec_receive_packet(pCodecCtx, pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      else if (ret < 0)
      {
        if (errorMsg.empty())
        {
          errorMsg = "Error: encoding error";
          SystemEventQueue::push("ffmpeg", errorMsg);
        }
        // Continue draining packets even on error
        break;
      }

      const std::string writeErr = handlePacket(pkt);
      av_packet_unref(pkt);
      if (!writeErr.empty())
      {
        if (errorMsg.empty())
        {
          errorMsg = writeErr;
        }
        break;
      }
    }
    if (overlap)
    {
      trimHistory();
    }

    return errorMsg;
  }

  bool supportsSplit() { return overlap; }

  uint64_t splitStartTimestamp(uint64_t splitTs)
  {
    if (!overlap || outputs.empty() || splitTs < firstTimestamp100ns)
    {
      return splitTs;
    }
    return captureTime(splitStartPts(toPts(splitTs)));
  }

  std::string splitVideoStream(std::string directory, std::string filename, uint64_t splitTs)
  {
    if (!overlap || !pCodecCtx || outputs.empty())
    {
      return "Error: Recorder cannot split files";
    }
    if (splitTs < firstTimestamp100ns)
    {
      const std::string msg = "Error: Frame timestamp precedes recording start";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    // The newest file may not have started yet when splits come faster than the encoder's
    // delay; it starts when its keyframe comes out.
    const int64_t splitPts = toPts(splitTs);
    OutputFile next;
    next.info.directory = directory;
    next.info.filename = filename;
    next.livePts = splitPts;
    next.startPts = splitStartPts(splitPts);
    outputs.back().endPts = splitPts;
    forceKeyFrame = true;
    outputs.push_back(next);

    // The overlap packets the encoder has already produced; the rest arrive as encoded.
    for (const BufferedPacket &buffered : history)
    {
      if (buffered.packet->pts >= outputs.back().startPts)
      {
        const std::string err = writePacket(outputs.back(), buffered.packet, buffered.ts100ns);
        if (!err.empty())
        {
          return err;
        }
      }
    }
    return "";
  }

  std::string stop()
  {
    std::string retval = "";
    if (pCodecCtx)
    {
      // Flush the encoder
      if (avcodec_send_frame(pCodecCtx, NULL) < 0)
      {
        auto msg = "Error: send frame to encoder failed";
        SystemEventQueue::push("ffmpeg", msg);
        retval = msg;
      }
    }

    // If pkt is null, it is uninitialized so we haven't read anything yet.
    if (pkt)
    {
      while (1)
      {
        int ret = avcodec_receive_packet(pCodecCtx, pkt);
        if (ret == AVERROR_EOF)
        {
          break;
        }
        else if (ret < 0)
        {
          auto msg = "Error: avcodec receive packet fail";
          SystemEventQueue::push("ffmpeg", msg);
          retval = msg;
          break;
        }
        // Keep draining after a write error so the other files still get their last frames.
        const std::string writeErr = handlePacket(pkt);
        av_packet_unref(pkt);
        if (retval.empty())
        {
          retval = writeErr;
        }
      }
    }

    // The encoder is drained, so every file is complete.
    while (!outputs.empty())
    {
      const std::string closeErr = closeFile(outputs.front());
      outputs.pop_front();
      if (retval.empty())
      {
        retval = closeErr;
      }
    }
    for (BufferedPacket &buffered : history)
    {
      av_packet_free(&buffered.packet);
    }
    history.clear();
    keyFramePts.clear();
    sentFrames.clear();

    if (pCodecCtx)
    {
      avcodec_free_context(&pCodecCtx);
    }
    if (pFrame)
    {
      av_frame_free(&pFrame);
    }
    if (pkt)
    {
      av_packet_free(&pkt);
    }
    if (sws_ctx)
    {
      sws_freeContext(sws_ctx);
    }

    pCodecCtx = nullptr;
    pFrame = nullptr;
    pkt = nullptr;
    sws_ctx = nullptr;
    return retval;
  }
  ~FFVideoRecorder() {}

private:
  int64_t toPts(uint64_t ts100ns) const
  {
    return av_rescale_q(ts100ns - firstTimestamp100ns, av_make_q(1, 10000000), av_make_q(1, 1000000));
  }

  // Capture time of the frame sent at pts.
  uint64_t captureTime(int64_t pts) const
  {
    auto it = std::lower_bound(sentFrames.begin(), sentFrames.end(), std::make_pair(pts, uint64_t(0)));
    return it != sentFrames.end() && it->first == pts ? it->second : firstTimestamp100ns + pts * 10;
  }

  // Where a file split at splitPts starts: the latest forced keyframe at least the overlap
  // before the split, give or take half a frame of capture jitter, but not before the
  // newest file's own live part.
  int64_t splitStartPts(int64_t splitPts) const
  {
    const int64_t overlapPts = int64_t(TimingConstants::RecordingOverlap100ns / 10);
    const int64_t halfFramePts = int64_t(500000 / av_q2d(pCodecCtx->framerate));
    int64_t start = outputs.back().livePts;
    for (int64_t key : keyFramePts)
    {
      if (key > start && key <= splitPts - overlapPts + halfFramePts)
      {
        start = key;
      }
    }
    return start;
  }

  // Drop what no split can need: anything before the latest forced keyframe at least the
  // overlap before the newest packet.
  void trimHistory()
  {
    if (history.empty())
    {
      return;
    }
    const int64_t limit = history.back().packet->pts - int64_t(TimingConstants::RecordingOverlap100ns / 10);
    int64_t cutoff = INT64_MIN;
    for (int64_t key : keyFramePts)
    {
      if (key <= limit)
      {
        cutoff = key;
      }
    }
    if (cutoff == INT64_MIN)
    {
      return;
    }
    while (!keyFramePts.empty() && keyFramePts.front() < cutoff)
    {
      keyFramePts.pop_front();
    }
    while (!history.empty() && history.front().packet->pts < cutoff)
    {
      av_packet_free(&history.front().packet);
      history.pop_front();
    }
    while (!sentFrames.empty() && sentFrames.front().first < cutoff)
    {
      sentFrames.pop_front();
    }
  }

  // Send an encoded packet to every file that holds it, and close a file once the next one
  // has started and the encoder has moved past the file's end.
  std::string handlePacket(const AVPacket *packet)
  {
    const uint64_t ts100ns = overlap ? captureTime(packet->pts) : firstTimestamp100ns + packet->pts * 10;
    if (overlap)
    {
      BufferedPacket buffered = {av_packet_clone(packet), ts100ns};
      history.push_back(buffered);
    }
    std::string err;
    // Newest file first, so the previous file knows whether the next one has started.
    for (size_t i = outputs.size(); i-- > 0;)
    {
      OutputFile &file = outputs[i];
      const bool nextStarted = i + 1 < outputs.size() && outputs[i + 1].started;
      if (packet->pts >= file.endPts && nextStarted)
      {
        continue;
      }
      const std::string writeErr = writePacket(file, packet, ts100ns);
      if (err.empty())
      {
        err = writeErr;
      }
    }
    for (size_t i = 0; i + 1 < outputs.size();)
    {
      if (packet->pts < outputs[i].endPts || !outputs[i + 1].started)
      {
        ++i;
        continue;
      }
      if (!outputs[i].started)
      {
        // Only if the encoder ignored forced keyframes; neighbouring files hold its frames.
        SystemEventQueue::push("ffmpeg", "Warning: " + outputs[i].info.filename +
                                             " skipped: no keyframe before the next file");
      }
      const std::string closeErr = closeFile(outputs[i]);
      outputs.erase(outputs.begin() + i);
      if (err.empty())
      {
        err = closeErr;
      }
      i = 0; // the file before may now be able to close
    }
    return err;
  }

  std::string writePacket(OutputFile &file, const AVPacket *packet, uint64_t ts100ns)
  {
    if (!file.started)
    {
      if (file.failed || !(packet->flags & AV_PKT_FLAG_KEY) || packet->pts < file.startPts)
      {
        return "";
      }
      if (packet->pts != file.startPts)
      {
        SystemEventQueue::push("ffmpeg", "Warning: " + file.info.filename +
                                             " starts late, at the next keyframe");
      }
      file.startPts = packet->pts;
      const std::string err = openMuxer(file);
      if (!err.empty())
      {
        // Free the partly opened muxer and do not retry; the file is never started.
        closeFile(file);
        file.failed = true;
        return err;
      }
    }
    AVPacket *out = av_packet_clone(packet);
    if (!out)
    {
      const std::string msg = "Error: Unable to allocate AVPacket";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    out->pts -= file.startPts;
    if (out->dts != AV_NOPTS_VALUE)
    {
      out->dts -= file.startPts;
    }
    av_packet_rescale_ts(out, pCodecCtx->time_base, file.stream->time_base);
    out->duration = av_rescale_q(1, av_inv_q(pCodecCtx->framerate), file.stream->time_base);
    out->stream_index = file.stream->index;
    const int ret = av_write_frame(file.formatCtx, out);
    av_packet_free(&out);
    if (ret < 0)
    {
      const std::string msg = "Error: Cannot write video frame";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    if (file.info.frames == 0)
    {
      file.info.firstTs100ns = ts100ns;
    }
    file.info.lastTs100ns = ts100ns;
    ++file.info.frames;
    return "";
  }

  // Create the file's tmp- MP4 and write its header; its first frame is at file.startPts.
  std::string openMuxer(OutputFile &file)
  {
    file.tmpPath = file.info.directory + "/tmp-" + file.info.filename + ".mp4";
    avformat_alloc_output_context2(&file.formatCtx, NULL, NULL, file.tmpPath.c_str());
    if (!file.formatCtx)
    {
      const auto msg = "Warning: Could not deduce output format from file extension: "
                       "using MPEG.";
      SystemEventQueue::push("ffmpeg", msg);
      avformat_alloc_output_context2(&file.formatCtx, NULL, "mpeg",
                                     file.tmpPath.c_str());
    }
    if (!file.formatCtx)
    {
      auto msg = "Error: Could not allocate format context";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    AVFormatContext *pFormatCtx = file.formatCtx;

    file.stream = avformat_new_stream(pFormatCtx, NULL);
    if (!file.stream)
    {
      auto msg = "Error: Could not allocate video stream";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    file.stream->id = pFormatCtx->nb_streams - 1;
    file.stream->time_base = pCodecCtx->time_base; // Use same timebase for both

    if (avcodec_parameters_from_context(file.stream->codecpar, pCodecCtx) < 0)
    {
      const auto msg = "Error: Could not copy codec parameters";
      SystemEventQueue::push("ffmpeg", msg);
    }

    if (!(pFormatCtx->oformat->flags & AVFMT_NOFILE))
    {
      if (avio_open(&pFormatCtx->pb, file.tmpPath.c_str(), AVIO_FLAG_WRITE) < 0)
      {
        auto msg = "Error: Could not open " + file.tmpPath;
        SystemEventQueue::push("ffmpeg", msg);
        return msg;
      }
    }

    // Optional: PRFT if your build supports it
    if (muxer_has_opt(pFormatCtx, "write_prft"))
    {
      av_opt_set_int(pFormatCtx->priv_data, "write_prft", 1, 0);
      av_opt_set(pFormatCtx->priv_data, "prft", "wallclock", 0);
    }
    // Every file counts from the recording's first frame, so a frame shared by two
    // overlapping files gets the same UTC time in both.
    long long utc_us = (firstTimestamp100ns + 5) / 10 + file.startPts; // 100ns to microseconds
    const std::string iso = iso8601_utc_now_us(static_cast<uint64_t>(utc_us));

    char utc_us_buf[32];
    std::snprintf(utc_us_buf, sizeof(utc_us_buf), "%lld", (long long)utc_us);

    // Stamp container + streams with custom key
    av_dict_set(&pFormatCtx->metadata, "creation_time", iso.c_str(), 0);
    av_dict_set(&pFormatCtx->metadata, "com.crewtimer.first_utc_us", utc_us_buf, 0);

    for (unsigned i = 0; i < pFormatCtx->nb_streams; ++i)
    {
      av_dict_set(&pFormatCtx->streams[i]->metadata, "creation_time", iso.c_str(), 0);
      av_dict_set(&pFormatCtx->streams[i]->metadata, "com.crewtimer.first_utc_us", utc_us_buf, 0);
    }

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "movflags", "use_metadata_tags", 0); // allow custom tags
    int err = avformat_write_header(pFormatCtx, &opts);
    av_dict_free(&opts);
    if (err < 0)
    {
      auto msg = "Error: Cannot write mp4 header";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    file.started = true;
    return "";
  }

  // Finish the file and rename it into place.
  std::string closeFile(OutputFile &file)
  {
    if (!file.formatCtx)
    {
      return "";
    }
    if (file.started)
    {
      // Properly close the output file
      av_write_trailer(file.formatCtx);
    }
    if (!(file.formatCtx->oformat->flags & AVFMT_NOFILE))
    {
      avio_closep(&file.formatCtx->pb);
    }
    avformat_free_context(file.formatCtx);
    file.formatCtx = nullptr;
    if (!file.started)
    {
      return "";
    }

    const std::string outputFile = file.info.directory + "/" + file.info.filename + ".mp4";
    if (std::rename(file.tmpPath.c_str(), outputFile.c_str()) != 0)
    {
      // If renaming failed, print an error message
      auto msg = "Error: Cannot rename file";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
    if (onFileClosed)
    {
      onFileClosed(file.info);
    }
    return "";
  }
};

std::shared_ptr<VideoRecorder> createFfmpegRecorder(int recordingQuality, bool forceSoftware, bool overlap)
{
  return std::shared_ptr<FFVideoRecorder>(new FFVideoRecorder(recordingQuality, forceSoftware, overlap));
}
