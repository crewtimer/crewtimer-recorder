// SrtReader.cpp
#include "VideoReader.hpp"
#include "srt/SrtReconnectingReader.hpp"
#include "SystemEventQueue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// FFmpeg
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
}

using namespace std::chrono;
#include <cstdint>

struct ApproxUtcResult
{
  int64_t utcMs;     // estimated UTC in ms
  int64_t unwrapped; // unwrapped DTS ticks near the reference
  int64_t kWraps;    // number of 2^33 wraps applied
};

namespace dtsutc
{
  static constexpr int64_t TICKS_PER_SEC = 90000;
  static constexpr int64_t PTS_MOD = (1LL << 33);
  static constexpr int64_t HALF_MOD = PTS_MOD / 2;
} // namespace dtsutc

inline ApproxUtcResult approxUtcFromDts_lockedToRef(uint64_t dtsTicks33, int64_t approxUtcMs)
{
  using namespace dtsutc;

  // 1) Lock to your "known within ~1s" wall-clock SECOND
  const int64_t refSec = (approxUtcMs >= 0) ? ((approxUtcMs + 500) / 1000) : ((approxUtcMs - 500) / 1000);
  const int64_t refTicks = refSec * TICKS_PER_SEC;

  // 2) Unwrap 33-bit DTS near refTicks (choose k that minimizes distance)
  const int64_t dtsTicks = static_cast<int64_t>(dtsTicks33 & (PTS_MOD - 1));
  const int64_t numer = refTicks - dtsTicks;
  const int64_t k = (numer >= 0)
                        ? ((numer + HALF_MOD) / PTS_MOD)
                        : ((numer - HALF_MOD) / PTS_MOD);
  const int64_t unwrapped = dtsTicks + k * PTS_MOD;

  // 3) Compute delta from the locked second in *ticks*, convert to ms.
  //    This avoids double rounding and second-boundary flips.
  const int64_t deltaTicks = unwrapped - refTicks;                                   // typically in (-45k..+45k)
  const int64_t deltaMs = (deltaTicks * 1000 + (TICKS_PER_SEC / 2)) / TICKS_PER_SEC; // rounded

  const int64_t utcMs = refSec * 1000 + deltaMs;
  return {utcMs, unwrapped, k};
}

/**
 * SRT + H.264 reader that decodes frames via FFmpeg and delivers UYVY422 frames
 * to the pipeline (to match NdiReader's downstream expectations).
 */
class SrtReader : public VideoReader
{
  class SrtFrame : public Frame
  {
    uint8_t *ownedBuf = nullptr;
    int ownedBufSize = 0;

  public:
    SrtFrame(uint8_t *data, int size, int strideBytes, Frame::PixelFormat fmt = Frame::PixelFormat::UYVY422)
    {
      ownedBuf = data;
      ownedBufSize = size;
      // Ensure base class won't attempt to delete[] this buffer; we free with av_free.
      this->ownData = false;
      this->data = data;
      this->stride = strideBytes;
      this->pixelFormat = fmt;
    }
    ~SrtFrame() override
    {
      if (ownedBuf)
      {
        av_free(ownedBuf);
        ownedBuf = nullptr;
      }
    }
  };

  int64_t encodingDelay = 0;
  uint64_t readerGeneration = 0;
  std::thread rxThread;
  std::atomic<bool> keepRunning{false};

  std::mutex listMutex;
  std::vector<CameraInfo> camList; // SRT has no discovery; keep empty.

  AddFrameFunction addFrameFunction = nullptr;
  std::string url; // Expected to be a full SRT URL (e.g., srt://…?mode=caller)

  // FFmpeg state
  AVCodecContext *vdecCtx = nullptr;
  AVStream *vStream = nullptr;
  int videoStreamIndex = -1;

  SwsContext *sws = nullptr;
  AVPixelFormat lastSwsSrcFmt = AV_PIX_FMT_NONE;
  int lastSwsSrcW = 0;
  int lastSwsSrcH = 0;
  int lastSwsDstW = 0;
  int lastSwsDstH = 0;
  AVRational timeBase{1, 1000}; // default fallback
  AVRational avgFrameRate{0, 1};
  int64_t startTimeMicroseconds = 0;
  int64_t startPts = AV_NOPTS_VALUE; // in video time_base
  mutable int64_t ptsWrapOffset = 0;
  mutable int64_t lastPtsRaw = AV_NOPTS_VALUE;

  // Startup sampling for FPS estimation
  std::vector<int64_t> init_unwrapped; // unwrapped PTS of each startup frame
  static constexpr size_t maxInitSamples = 600;

  std::shared_ptr<SrtReconnectingReader> reader;

  // Recent per-frame timing, logged when the timeline steps backward or jumps
  // forward so the cause (sender clock, B-frame ordering, packet loss) can be
  // identified.
  struct FrameTiming
  {
    int64_t pts;
    int64_t pktDts;
    int64_t ts100ns;
    int64_t receivedUs;
    char pictType;
    bool key;
    bool corrupt;
    bool discarded;
  };
  std::deque<FrameTiming> recentFrames;
  static constexpr size_t recentFrameLimit = 8;

  void rememberFrame(const FrameTiming &timing)
  {
    recentFrames.push_back(timing);
    if (recentFrames.size() > recentFrameLimit)
      recentFrames.pop_front();
  }

  static std::string describeFrame(const FrameTiming &f)
  {
    std::stringstream ss;
    ss << "pts=" << f.pts << " pktDts=" << f.pktDts << " type=" << f.pictType
       << (f.key ? " key" : "") << (f.corrupt ? " corrupt" : "")
       << (f.discarded ? " discarded" : "")
       << " ts100ns=" << f.ts100ns
       << " arrivalMinusTsMs=" << std::fixed << std::setprecision(1)
       << (f.receivedUs * 10 - f.ts100ns) / 1e4;
    return ss.str();
  }

  void logTimestampDiscontinuity(const std::string &what, const FrameTiming &current, int64_t lastTS100ns,
                                 bool includeHistory)
  {
    std::stringstream ss;
    ss << what << ": step=" << std::fixed << std::setprecision(3)
       << (current.ts100ns - lastTS100ns) / 1e4 << "ms " << describeFrame(current)
       << " startPts=" << startPts << " wrapOffset=" << ptsWrapOffset
       << " encodingDelayMs=" << encodingDelay / 1e4
       << " timeBase=" << timeBase.num << "/" << timeBase.den;
    SystemEventQueue::push("SRT", ss.str());
    if (!includeHistory)
      return;
    for (size_t i = 0; i < recentFrames.size(); ++i)
    {
      const auto &f = recentFrames[i];
      const int64_t prevTs = i ? recentFrames[i - 1].ts100ns : f.ts100ns;
      std::stringstream line;
      line << "  prior[-" << (recentFrames.size() - i) << "] " << describeFrame(f)
           << " stepMs=" << std::fixed << std::setprecision(3) << (f.ts100ns - prevTs) / 1e4;
      SystemEventQueue::push("SRT", line.str());
    }
  }

  bool openInput()
  {
    SrtReconnectConfig cfg;
    cfg.url = url;
    cfg.open_timeout_ms = 8000;
    cfg.read_timeout_ms = 5000;
    cfg.max_retries = -1; // forever
    cfg.base_backoff_ms = 250;
    cfg.max_backoff_ms = 3000;

    reader = std::make_shared<SrtReconnectingReader>(cfg);
    // Register disconnect callback so higher layers can react (emit a SOURCE_DISCONNECTED frame)
    reader->setDisconnectCallback([this]()
                                  {
      SystemEventQueue::push("SRT", "Warning: Disconnected: " + url);
      if (addFrameFunction)
      {
        auto f = std::make_shared<Frame>();
        f->frameType = Frame::FrameType::SOURCE_DISCONNECTED;
        addFrameFunction(f);
      } });

    if (!reader->open())
    {
      std::cerr << "Initial open failed.\n";
      reader = nullptr;
      return false;
    }

    readerGeneration = reader->connectionGeneration();
    SystemEventQueue::push("SRT", "Connected: " + url);
    if (!refreshStreamInfo())
    {
      SystemEventQueue::push("SRT", "Error: No video stream in SRT input");
      return false;
    }
    resetTimingCalibration();

    // Try hardware decoders in platform-specific order, fall back to software.
    // name=nullptr means "use the stream's own codec ID decoder with this HW device"
    // (VideoToolbox and similar hwaccel-based decoders have no separate named decoder)
    struct HwDecoderSpec { const char *name; AVHWDeviceType hwType; };
#if defined(__APPLE__)
    static const HwDecoderSpec hwSpecs[] = {
        // VideoToolbox has no separate named decoder — attach device to std h264 decoder
        {nullptr, AV_HWDEVICE_TYPE_VIDEOTOOLBOX},
    };
#elif defined(_WIN32)
    static const HwDecoderSpec hwSpecs[] = {
        {"h264_cuvid", AV_HWDEVICE_TYPE_CUDA},   // NVIDIA NVDEC (named decoder)
        {"h264_qsv",   AV_HWDEVICE_TYPE_QSV},    // Intel Quick Sync (named decoder)
        {nullptr,      AV_HWDEVICE_TYPE_D3D11VA}, // any D3D11 GPU via hwaccel (AMD, integrated, etc.)
    };
#else
    static const HwDecoderSpec hwSpecs[] = {
        {"h264_cuvid",   AV_HWDEVICE_TYPE_CUDA},
        {"h264_vaapi",   AV_HWDEVICE_TYPE_VAAPI},
        {"h264_v4l2m2m", AV_HWDEVICE_TYPE_NONE},
    };
#endif
    static const int hwSpecCount = sizeof(hwSpecs) / sizeof(hwSpecs[0]);

    for (int i = 0; i < hwSpecCount && !vdecCtx; ++i)
    {
      const AVCodec *candidate = hwSpecs[i].name
          ? avcodec_find_decoder_by_name(hwSpecs[i].name)
          : avcodec_find_decoder(vStream->codecpar->codec_id);
      if (!candidate)
        continue;
      AVCodecContext *testCtx = avcodec_alloc_context3(candidate);
      if (!testCtx)
        continue;
      if (avcodec_parameters_to_context(testCtx, vStream->codecpar) < 0)
      {
        avcodec_free_context(&testCtx);
        continue;
      }
      testCtx->thread_count = 0;
      if (hwSpecs[i].hwType != AV_HWDEVICE_TYPE_NONE)
      {
        AVBufferRef *dev = nullptr;
        if (av_hwdevice_ctx_create(&dev, hwSpecs[i].hwType, nullptr, nullptr, 0) < 0)
        {
          avcodec_free_context(&testCtx);
          continue;
        }
        testCtx->hw_device_ctx = av_buffer_ref(dev);
        av_buffer_unref(&dev);
      }
      if (avcodec_open2(testCtx, candidate, nullptr) >= 0)
      {
        vdecCtx = testCtx;
        std::string hwLabel = hwSpecs[i].name
            ? hwSpecs[i].name
            : (std::string(candidate->name) + "+" + av_hwdevice_get_type_name(hwSpecs[i].hwType));
        SystemEventQueue::push("SRT", "Hardware decoder: " + hwLabel);
      }
      else
      {
        avcodec_free_context(&testCtx);
      }
    }

    if (!vdecCtx)
    {
      // Software decoder fallback
      const AVCodec *vdec = avcodec_find_decoder(vStream->codecpar->codec_id);
      if (!vdec)
      {
        SystemEventQueue::push("SRT", "Error: No decoder for video codec");
        return false;
      }
      vdecCtx = avcodec_alloc_context3(vdec);
      if (!vdecCtx)
      {
        SystemEventQueue::push("SRT", "Error: avcodec_alloc_context3 failed");
        return false;
      }
      if (avcodec_parameters_to_context(vdecCtx, vStream->codecpar) < 0)
      {
        SystemEventQueue::push("SRT", "Error: avcodec_parameters_to_context failed");
        avcodec_free_context(&vdecCtx);
        vdecCtx = nullptr;
        if (reader) { reader->close(); reader = nullptr; }
        return false;
      }
      vdecCtx->thread_count = 0;
      if (avcodec_open2(vdecCtx, vdec, nullptr) < 0)
      {
        SystemEventQueue::push("SRT", "Error: avcodec_open2 failed");
        avcodec_free_context(&vdecCtx);
        vdecCtx = nullptr;
        if (reader) { reader->close(); reader = nullptr; }
        return false;
      }
      SystemEventQueue::push("SRT", "Software decoder: " + std::string(vdec->name));
    }

    timeBase = vStream->time_base.num && vStream->time_base.den ? vStream->time_base : AVRational{1, 1000};
    avgFrameRate = vStream->avg_frame_rate.num && vStream->avg_frame_rate.den ? vStream->avg_frame_rate : AVRational{0, 1};

    SystemEventQueue::push("Debug", "SRT stream open: " + url);
    return true;
  }

  void closeInput()
  {
    if (sws)
    {
      sws_freeContext(sws);
      sws = nullptr;
    }
    lastSwsSrcFmt = AV_PIX_FMT_NONE;
    lastSwsSrcW = 0;
    lastSwsSrcH = 0;
    lastSwsDstW = 0;
    lastSwsDstH = 0;
    if (vdecCtx)
    {
      avcodec_free_context(&vdecCtx);
      vdecCtx = nullptr;
    }

    vStream = nullptr;
    videoStreamIndex = -1;
  }

  void resetTimingCalibration()
  {
    startPts = AV_NOPTS_VALUE;
    ptsWrapOffset = 0;
    lastPtsRaw = AV_NOPTS_VALUE;
    encodingDelay = 0;
    startTimeMicroseconds = 0;
    // reset startup sampling
    init_unwrapped.clear();
  }

  // Collect startup PTS samples and decide when to finish startup.
  // Returns true when startup has completed and normal processing should continue.
  bool startup_collect_and_maybe_finish(AVFrame *frm, int64_t pts, int frameCount, double &msPerFrame, AVRational &frameRate)
  {
    if (pts != AV_NOPTS_VALUE)
    {
      int64_t un = unwrap_pts(pts);
      if (un != AV_NOPTS_VALUE && init_unwrapped.size() < maxInitSamples)
        init_unwrapped.push_back(un);
    }

    // looking for when to start: need at least 16 frames and a keyframe
    if (frameCount >= 16 && (frm->flags & AV_FRAME_FLAG_KEY))
    {
      std::cerr << "SRTReader: startup complete at frameCount=" << frameCount << " pts=" << pts << "\n";

      // Use the median frame interval: a single timestamp step back or jump
      // within the startup samples would skew (last - first) / count.
      std::vector<int64_t> deltas;
      int backwardSteps = 0;
      for (size_t i = 1; i < init_unwrapped.size(); ++i)
      {
        const int64_t d = init_unwrapped[i] - init_unwrapped[i - 1];
        if (d > 0)
          deltas.push_back(d);
        else
          ++backwardSteps;
      }
      double fpsEstimate = 0.0;
      int64_t minDelta = 0, maxDelta = 0, medianDelta = 0;
      if (!deltas.empty())
      {
        std::sort(deltas.begin(), deltas.end());
        minDelta = deltas.front();
        maxDelta = deltas.back();
        medianDelta = deltas[deltas.size() / 2];
        fpsEstimate = 1.0 / (static_cast<double>(medianDelta) * av_q2d(vStream->time_base));
      }

      std::stringstream ss;
      ss << "Startup frame interval: " << init_unwrapped.size() << " samples, median=" << medianDelta
         << " min=" << minDelta << " max=" << maxDelta << " ticks, " << backwardSteps
         << " backward steps, stream avg_frame_rate=" << avgFrameRate.num << "/" << avgFrameRate.den
         << ", estimate=" << std::fixed << std::setprecision(3) << fpsEstimate << " fps";
      SystemEventQueue::push("SRT", ss.str());

      constexpr double minPlausibleFps = 1.0;
      // Matches the encoder's limit in FFRecorder
      constexpr double maxPlausibleFps = 120.0;
      if (fpsEstimate < minPlausibleFps || fpsEstimate > maxPlausibleFps)
      {
        const double streamFps = avgFrameRate.den ? av_q2d(avgFrameRate) : 0.0;
        const bool useStream = streamFps >= minPlausibleFps && streamFps <= maxPlausibleFps;
        SystemEventQueue::push("SRT", "Warning: implausible frame rate estimate; using " +
                                          (useStream ? "stream rate " + std::to_string(streamFps)
                                                     : "previous rate " + std::to_string(av_q2d(frameRate))) +
                                          " fps");
        fpsEstimate = useStream ? streamFps : 0.0;
      }

      if (fpsEstimate > 0.0)
      {
        int64_t num = static_cast<int64_t>(std::llround(fpsEstimate * 1000.0));
        int64_t den = 1000;
        frameRate.num = static_cast<int>(num);
        frameRate.den = static_cast<int>(den);
        msPerFrame = 1000.0 / fpsEstimate;
        std::cerr << "SRTReader: estimated FPS from startup samples = " << fpsEstimate << " => " << frameRate.num << "/" << frameRate.den << "\n";
      }

      anchorTiming(pts);
      return true;
    }

    // still in startup; caller should skip this frame
    return false;
  }

  // Anchor the PTS timeline to the current wall clock and camera clock offset.
  void anchorTiming(int64_t pts)
  {
    resetTimingCalibration();
    int64_t utcMilli = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    auto approxUtc = approxUtcFromDts_lockedToRef(pts, utcMilli);
    SystemEventQueue::push("SRT", "pts=" + std::to_string(pts) + " Approx UTC: " + std::to_string(approxUtc.utcMs) + " ms delta=" + std::to_string(utcMilli - approxUtc.utcMs));
    encodingDelay = (utcMilli - approxUtc.utcMs) * 10000;
    set_start_pts(pts);
    startTimeMicroseconds = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
  }

  bool refreshStreamInfo()
  {
    if (!reader)
      return false;
    AVFormatContext *fmtCtx = reader->formatContext();
    if (!fmtCtx)
      return false;

    int candidateIndex = -1;
    if (videoStreamIndex >= 0 && videoStreamIndex < static_cast<int>(fmtCtx->nb_streams))
    {
      AVStream *candidate = fmtCtx->streams[videoStreamIndex];
      if (candidate && candidate->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        candidateIndex = videoStreamIndex;
    }
    if (candidateIndex < 0)
    {
      for (unsigned i = 0; i < fmtCtx->nb_streams; ++i)
      {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
          candidateIndex = static_cast<int>(i);
          break;
        }
      }
    }
    if (candidateIndex < 0)
    {
      SystemEventQueue::push("SRT", "Error: Video stream not found after reconnect");
      return false;
    }

    videoStreamIndex = candidateIndex;
    vStream = fmtCtx->streams[videoStreamIndex];
    timeBase = vStream->time_base.num && vStream->time_base.den ? vStream->time_base : AVRational{1, 1000};
    avgFrameRate = vStream->avg_frame_rate.num && vStream->avg_frame_rate.den ? vStream->avg_frame_rate : AVRational{0, 1};

    std::cerr << "SRTReader: video stream index=" << videoStreamIndex
              << " time_base=" << timeBase.num << "/" << timeBase.den
              << " avg_frame_rate=" << avgFrameRate.num << "/" << avgFrameRate.den
              << "\n";
    return true;
  }

  bool set_start_pts(int64_t pts)
  {
    if (pts == AV_NOPTS_VALUE)
      return false;
    startPts = pts;
    ptsWrapOffset = 0;
    lastPtsRaw = pts;
    return true;
  }

  int64_t unwrap_pts(int64_t pts) const
  {
    if (pts == AV_NOPTS_VALUE)
      return AV_NOPTS_VALUE;
    if (!vStream)
      return pts;

    const int wrapBits = vStream->pts_wrap_bits;
    if (wrapBits <= 0 || wrapBits >= 63)
    {
      lastPtsRaw = pts;
      return pts;
    }

    const int64_t wrapPeriod = 1LL << wrapBits;
    if (lastPtsRaw != AV_NOPTS_VALUE)
    {
      int64_t diff = pts - lastPtsRaw;
      if (diff < -(wrapPeriod >> 1))
      {
        ptsWrapOffset += wrapPeriod;
      }
    }

    lastPtsRaw = pts;
    return pts + ptsWrapOffset;
  }

  // compute elapsed seconds in current segment based on video PTS
  double segment_elapsed_sec(int64_t pts) const
  {
    if (startPts == AV_NOPTS_VALUE)
      return 0.0;
    if (pts == AV_NOPTS_VALUE)
      return 0.0;
    int64_t unwrappedPts = unwrap_pts(pts);
    int64_t diff = unwrappedPts - startPts;
    auto elapsed = diff * av_q2d(vStream->time_base);
    return elapsed;
  }

  int64_t pts_to_100ns(int64_t pts) const
  {
    auto secs = segment_elapsed_sec(pts);
    int64_t secs100ns = static_cast<int64_t>(secs * 1.0e7);
    auto ts100ns = startTimeMicroseconds * 10 + secs100ns;
    ts100ns -= encodingDelay;
    return ts100ns;
  }

  void run()
  {
    if (!openInput())
    {
      return;
    }

    lastSwsSrcFmt = AV_PIX_FMT_NONE;
    lastSwsSrcW = 0;
    lastSwsSrcH = 0;
    lastSwsDstW = 0;
    lastSwsDstH = 0;

    if (!refreshStreamInfo())
    {
      closeInput();
      return;
    }

    // Diagnostics similar to NdiReader (duplicate/gaps)
    int64_t lastTS100ns = 0;
    int64_t frameCount = 0;
    startPts = AV_NOPTS_VALUE;
    ptsWrapOffset = 0;
    lastPtsRaw = AV_NOPTS_VALUE;
    AVRational frameRate{60, 1};
    double msPerFrame = 1000 / 60.0;
    // A normal SRT frame interval is measured in milliseconds.  A jump this
    // large means the sender's timestamp clock changed (for example, after a
    // camera reboot) even if the transport connection itself stayed alive.
    constexpr int64_t maxContinuousTimestampStep100ns = 30LL * 10000000LL;
    // The recorder and review app require strictly increasing frame times; 1us
    // is the encoder's time base resolution.
    constexpr int64_t minTimestampStep100ns = 10;
    // Frames that do not advance past the last delivered frame are discarded.
    // If that persists this long, the sender timeline has moved back for good:
    // close the current file and start a new one on the new timeline.
    constexpr auto maxDiscardDuration = std::chrono::seconds(1);
    int64_t discardedFrames = 0;
    int64_t maxDiscardBehind100ns = 0;
    int discardEpisodes = 0;
    // Full frame history is logged for the first few episodes and for every
    // restart; later short episodes get a one-line summary.
    constexpr int detailedDiscardEpisodes = 10;
    steady_clock::time_point discardStart;
    recentFrames.clear();

    // Startup sampling state is kept in member fields (init_unwrapped)
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc();
    AVFrame *sw_frm = av_frame_alloc(); // reusable staging frame for HW→CPU download

    // Hardware decoders (especially VideoToolbox) require the first packet fed to
    // them to be a keyframe (IDR).  Feeding P/B frames before the first IDR causes
    // "hardware accelerator failed to decode picture" errors.  Gate delivery until
    // we see a keyframe; re-arm after every decoder flush (reconnect).
    bool waitingForKeyframe = true;

    while (keepRunning)
    {
      int ret = reader->readFrame(pkt);
      if (ret < 0)
      {
        // End or error: try a brief sleep and continue; if persistent, break
        av_packet_unref(pkt);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      uint64_t currentGeneration = reader->connectionGeneration();
      if (currentGeneration != readerGeneration)
      {
        readerGeneration = currentGeneration;
        SystemEventQueue::push("SRT", "Reconnecting: " + url);
        // The reconnect freed the previous AVFormatContext; vStream would
        // otherwise point into it and timestamps would be converted with a
        // garbage time base.
        if (!refreshStreamInfo())
        {
          // No video stream on this connection: drop every packet until the
          // next reconnect rather than touching the freed stream.
          vStream = nullptr;
          videoStreamIndex = -1;
          av_packet_unref(pkt);
          continue;
        }
        avcodec_flush_buffers(vdecCtx);
        waitingForKeyframe = true;
        frameCount = 0;
        lastTS100ns = 0;
        discardedFrames = 0;
        discardEpisodes = 0;
        recentFrames.clear();
        resetTimingCalibration();

        av_packet_unref(pkt);
        continue;
      }

      if (pkt->stream_index != videoStreamIndex)
      {
        av_packet_unref(pkt);
        continue;
      }

      if (waitingForKeyframe)
      {
        if (!(pkt->flags & AV_PKT_FLAG_KEY))
        {
          av_packet_unref(pkt);
          continue;
        }
        waitingForKeyframe = false;
      }

      // Send / Receive
      if (avcodec_send_packet(vdecCtx, pkt) == 0)
      {
        while (avcodec_receive_frame(vdecCtx, frm) == 0)
        {
          frameCount++;

          // Download HW frame to CPU memory if needed
          AVFrame *srcFrm = frm;
          if (frm->hw_frames_ctx != nullptr)
          {
            av_frame_unref(sw_frm);
            if (av_hwframe_transfer_data(sw_frm, frm, 0) < 0)
            {
              SystemEventQueue::push("SRT", "Warning: hw frame transfer failed, skipping");
              av_frame_unref(frm);
              continue;
            }
            sw_frm->pts = frm->pts;
            sw_frm->best_effort_timestamp = frm->best_effort_timestamp;
            sw_frm->flags = frm->flags;
            srcFrm = sw_frm;
          }

          // Timestamp handling
          int64_t pts = (srcFrm->best_effort_timestamp == AV_NOPTS_VALUE)
                            ? srcFrm->pts
                            : srcFrm->best_effort_timestamp;

          // Without a timestamp a frame cannot be placed on the timeline or
          // used to anchor it.
          if (pts == AV_NOPTS_VALUE)
          {
            std::cerr << "SRTReader: dropping frame without a timestamp\n";
            av_frame_unref(frm);
            continue;
          }

          if (startPts == AV_NOPTS_VALUE)
          {
            bool started = startup_collect_and_maybe_finish(srcFrm, pts, static_cast<int>(frameCount), msPerFrame, frameRate);
            if (!started)
            {
              av_frame_unref(frm);
              continue;
            }
          }

          // Every 5 minutes, log drift info
          if (frameCount % (60 * 60 * 5) == 0)
          {
            int64_t utcMilli = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            auto approxUtc = approxUtcFromDts_lockedToRef(pts, utcMilli);
            auto delay = (utcMilli - approxUtc.utcMs);
            SystemEventQueue::push("SRT", "pts=" + std::to_string(pts) + " Approx UTC: " + std::to_string(approxUtc.utcMs) + " ms delta=" + std::to_string(delay));
          }

          int64_t ts100ns = pts_to_100ns(pts);

          FrameTiming timing{pts,
                             frm->pkt_dts,
                             ts100ns,
                             duration_cast<microseconds>(system_clock::now().time_since_epoch()).count(),
                             av_get_picture_type_char(frm->pict_type),
                             (frm->flags & AV_FRAME_FLAG_KEY) != 0,
                             (frm->flags & AV_FRAME_FLAG_CORRUPT) != 0 || frm->decode_error_flags != 0,
                             false};

          // A camera can reboot and restart its MPEG timestamp clock without
          // causing SRT to establish a new connection generation.  Continuing
          // to unwrap against the old clock produces enormous gaps and leaves
          // every subsequent frame incorrectly timestamped.  The decoder is
          // already running, so re-anchor on this frame rather than re-running
          // the startup sampling, which would drop frames until the next
          // keyframe.  If the new anchor lands before the last delivered frame,
          // the backward-step handling below takes over.
          if (lastTS100ns > 0 && ts100ns - lastTS100ns > maxContinuousTimestampStep100ns)
          {
            logTimestampDiscontinuity("Timestamp jumped forward; recalibrating", timing, lastTS100ns, true);
            anchorTiming(pts);
            ts100ns = timing.ts100ns = pts_to_100ns(pts);
          }

          if (lastTS100ns > 0 && ts100ns < lastTS100ns + minTimestampStep100ns)
          {
            const auto now = steady_clock::now();
            if (discardedFrames == 0)
            {
              discardStart = now;
              maxDiscardBehind100ns = 0;
              ++discardEpisodes;
              if (discardEpisodes <= detailedDiscardEpisodes)
                logTimestampDiscontinuity("Timestamp stepped back; discarding until it passes the last frame",
                                          timing, lastTS100ns, true);
            }
            ++discardedFrames;
            maxDiscardBehind100ns = std::max(maxDiscardBehind100ns, lastTS100ns - ts100ns);

            if (now - discardStart <= maxDiscardDuration)
            {
              timing.discarded = true;
              rememberFrame(timing);
              av_frame_unref(frm);
              continue;
            }

            std::stringstream ss;
            ss << "Timestamps behind the last frame for "
               << duration_cast<milliseconds>(now - discardStart).count() << "ms (" << discardedFrames
               << " frames discarded, up to " << maxDiscardBehind100ns / 10000
               << "ms behind); restarting recording on the new timeline";
            logTimestampDiscontinuity(ss.str(), timing, lastTS100ns, true);
            if (addFrameFunction)
            {
              auto f = std::make_shared<Frame>();
              f->frameType = Frame::FrameType::SOURCE_DISCONNECTED;
              addFrameFunction(f);
            }
            anchorTiming(pts);
            ts100ns = timing.ts100ns = pts_to_100ns(pts);
            lastTS100ns = 0;
            discardedFrames = 0;
          }
          else if (discardedFrames > 0)
          {
            std::stringstream ss;
            ss << "Timestamps passed the last frame after "
               << duration_cast<milliseconds>(steady_clock::now() - discardStart).count() << "ms ("
               << discardedFrames << " frames discarded, up to " << std::fixed << std::setprecision(3)
               << maxDiscardBehind100ns / 1e4 << "ms behind, episode " << discardEpisodes << ")";
            logTimestampDiscontinuity(ss.str(), timing, lastTS100ns, false);
            discardedFrames = 0;
          }
          rememberFrame(timing);

          // Initialize sws lazily for non-YUV420P sources (e.g. NV12 from HW decoders)
          auto srcFmt = static_cast<AVPixelFormat>(srcFrm->format);
          const int outW = srcFrm->width & ~1;
          const int outH = srcFrm->height & ~1;
          if (outW <= 0 || outH <= 0)
          {
            SystemEventQueue::push("SRT", "Warning: decoded frame has invalid dimensions, skipping");
            av_frame_unref(frm);
            continue;
          }

          if (srcFmt != AV_PIX_FMT_YUV420P &&
              (sws == nullptr ||
               lastSwsSrcFmt != srcFmt ||
               lastSwsSrcW != srcFrm->width ||
               lastSwsSrcH != srcFrm->height ||
               lastSwsDstW != outW ||
               lastSwsDstH != outH))
          {
            if (sws) sws_freeContext(sws);
            sws = sws_getContext(srcFrm->width, srcFrm->height, srcFmt,
                                 outW, outH, AV_PIX_FMT_YUV420P,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
            lastSwsSrcFmt = srcFmt;
            lastSwsSrcW = srcFrm->width;
            lastSwsSrcH = srcFrm->height;
            lastSwsDstW = outW;
            lastSwsDstH = outH;
            if (!sws)
            {
              SystemEventQueue::push("SRT", "Error: sws_getContext failed");
              av_frame_unref(frm);
              continue;
            }
          }

          // Allocate I420 output buffer
          int i420Size = outW * outH * 3 / 2;
          uint8_t *i420Buf = static_cast<uint8_t *>(av_malloc(i420Size));
          if (!i420Buf)
          {
            SystemEventQueue::push("SRT", "Error: av_malloc failed for I420 buffer");
            av_frame_unref(frm);
            continue;
          }

          if (srcFmt == AV_PIX_FMT_YUV420P)
          {
            // Direct plane copy — no color space conversion needed
            for (int y = 0; y < outH; y++)
              memcpy(i420Buf + y * outW, srcFrm->data[0] + y * srcFrm->linesize[0], outW);
            for (int y = 0; y < outH / 2; y++)
              memcpy(i420Buf + outW * outH + y * (outW / 2), srcFrm->data[1] + y * srcFrm->linesize[1], outW / 2);
            for (int y = 0; y < outH / 2; y++)
              memcpy(i420Buf + outW * outH * 5 / 4 + y * (outW / 2), srcFrm->data[2] + y * srcFrm->linesize[2], outW / 2);
          }
          else
          {
            // Convert to YUV420P (handles NV12 from hardware decoders, etc.)
            uint8_t *dstPlanes[4] = {i420Buf, i420Buf + outW * outH, i420Buf + outW * outH * 5 / 4, nullptr};
            int dstStrides[4] = {outW, outW / 2, outW / 2, 0};
            sws_scale(sws, srcFrm->data, srcFrm->linesize, 0, srcFrm->height, dstPlanes, dstStrides);
          }

          // Gap/duplicate diagnostics
          if (lastTS100ns != 0)
          {
            int64_t deltaMs = (ts100ns - lastTS100ns) / 10000;
            if (deltaMs == 0 || deltaMs >= (int)(2 * msPerFrame))
            {
              std::stringstream msg;
              if (deltaMs == 0)
                msg << "Duplicate frame timestamp";
              else
              {
                int64_t framesMissing = static_cast<int64_t>(std::llround(static_cast<double>(deltaMs) / msPerFrame - 1.0));
                msg << "Gap=" << deltaMs << "ms (" << framesMissing << " frames missing)" << " assuming " << msPerFrame << "ms/frame";
              }
              std::cerr << msg.str() << std::endl;
              if ((deltaMs >= 110) || reportAllGaps)
                SystemEventQueue::push("SRT", std::string("Warning: ") + msg.str());
            }
          }

          lastTS100ns = ts100ns;

          auto out = std::make_shared<SrtFrame>(i420Buf, i420Size, outW, Frame::PixelFormat::YUV420P);
          out->xres = outW;
          out->yres = outH;
          out->timestamp = ts100ns;
          out->receivedTs100ns = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count() * 10;
          out->frame_rate_N = frameRate.num ? frameRate.num : 60000;
          out->frame_rate_D = frameRate.den ? frameRate.den : 1001;
          if (addFrameFunction)
            addFrameFunction(out);

          av_frame_unref(frm);
        }

        av_packet_unref(pkt);
      }
    }
    av_frame_free(&sw_frm);
    av_frame_free(&frm);
    av_packet_free(&pkt);
    closeInput();
  }

public:
  SrtReader()
  {
    // One-time FFmpeg init (safe to call multiple times on recent FFmpeg)
    av_log_set_level(AV_LOG_ERROR);
    avformat_network_init();
  }

  std::vector<CameraInfo> getCameraList() override
  {
    // No discovery for SRT
    return std::vector<VideoReader::CameraInfo>();
  }

  std::string start(const CameraInfo &camera, AddFrameFunction cb) override
  {

    const auto ipAddress = camera.address;
    const auto port = 1600;
    SystemEventQueue::push("SRT", "Connecting to " + camera.name);
    url = "srt://" + ipAddress + ":" + std::to_string(port) + "?mode=caller&transtype=live&latency=120&streamid=r=0";

    addFrameFunction = cb;

    if (rxThread.joinable())
      stop();

    keepRunning = true;
    rxThread = std::thread(&SrtReader::run, this);
    return "";
  }

  std::string stop() override
  {
    SystemEventQueue::push("SRT", "Stopping SRT reader");
    keepRunning = false;
    if (rxThread.joinable())
      rxThread.join();
    addFrameFunction = nullptr;
    if (reader)
    {
      reader->close();
      reader = nullptr;
    }
    return "";
  }

  ~SrtReader() override
  {
    stop();
    avformat_network_deinit();
  }
};

// Factory (mirror your NDI factory)
std::shared_ptr<VideoReader> createSrtReader()
{
  return std::shared_ptr<SrtReader>(new SrtReader());
}
