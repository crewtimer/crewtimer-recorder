#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
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
#include "VideoRecorder.hpp"

class FFVideoRecorder : public VideoRecorder
{
  int frame_index;
  AVFrame *pFrame = nullptr;
  AVPacket *pkt = nullptr;
  struct SwsContext *sws_ctx = nullptr;
  AVFormatContext *pFormatCtx = nullptr;
  AVCodecContext *pCodecCtx = nullptr;
  AVStream *video_st = nullptr;
  std::string outputFile;
  std::string tmpFile;
  std::string codecName;
  bool streamInitialized = false;
  bool headerWritten = false;
  bool usePacketInput = false;
  int pendingWidth = 0;
  int pendingHeight = 0;
  float pendingFps = 0.f;
  uint64_t pendingTimestamp = 0;
  AVRational pendingTimeBase{0, 1};
  AVRational pendingFrameRate{0, 1};
  AVRational passthroughTimeBase{0, 1};
  AVRational passthroughFrameRate{0, 1};
  std::string isoCreationTime;
  long long utcCreationUs = 0;

  std::string initializeContainer(std::string directory, std::string filename, uint64_t timestamp);
  std::string initializeEncoderPipeline();
  std::string initializePacketPipeline(const FramePtr &firstPacket);
  std::string encodeAndWriteFrame(FramePtr video_frame);
  std::string writeEncodedPacket(FramePtr video_frame);
  void stampStreamMetadata();
  void resetState();

public:
  FFVideoRecorder()
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

  static std::string iso8601_utc_now_ms(uint64_t ms)
  {
    // using namespace std::chrono;
    // auto now = time_point_cast<milliseconds>(system_clock::now());
    // auto ms = now.time_since_epoch().count();
    std::time_t secs = ms / 1000;
    int msec = static_cast<int>(ms % 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &secs);
#else
    gmtime_r(&secs, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
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
                              int width, int height, float fps, uint64_t timestamp) override;

  std::string writeVideoFrame(FramePtr video_frame) override;

  std::string stop() override;
  ~FFVideoRecorder() {}
};

void FFVideoRecorder::resetState()
{
  if (sws_ctx)
  {
    sws_freeContext(sws_ctx);
    sws_ctx = nullptr;
  }
  if (pFrame)
  {
    av_frame_free(&pFrame);
  }
  if (pkt)
  {
    av_packet_unref(pkt);
    av_packet_free(&pkt);
  }
  if (pCodecCtx)
  {
    avcodec_free_context(&pCodecCtx);
  }
  video_st = nullptr;
  streamInitialized = false;
  headerWritten = false;
  usePacketInput = false;
  passthroughTimeBase = AVRational{0, 1};
  passthroughFrameRate = AVRational{0, 1};
}

std::string FFVideoRecorder::initializeContainer(std::string directory, std::string filename, uint64_t timestamp)
{
  std::string baseName = filename + ".mp4";
  outputFile = directory + "/" + baseName;
  tmpFile = directory + "/tmp-" + baseName;

  if (pFormatCtx)
  {
    avformat_free_context(pFormatCtx);
    pFormatCtx = nullptr;
  }

  av_log_set_level(AV_LOG_ERROR);
  avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, tmpFile.c_str());
  if (!pFormatCtx)
  {
    const auto msg = "Could not deduce output format from file extension: using MPEG.";
    SystemEventQueue::push("ffmpeg", msg);
    avformat_alloc_output_context2(&pFormatCtx, NULL, "mpeg", tmpFile.c_str());
  }
  if (!pFormatCtx)
  {
    auto msg = "Could not allocate format context";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }

  if (!(pFormatCtx->oformat->flags & AVFMT_NOFILE))
  {
    if (avio_open(&pFormatCtx->pb, tmpFile.c_str(), AVIO_FLAG_WRITE) < 0)
    {
      auto msg = "Error: Could not open " + tmpFile;
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }
  }

  pendingTimestamp = timestamp;
  utcCreationUs = (timestamp + 5) / 10;                          // 100ns to microseconds
  isoCreationTime = iso8601_utc_now_ms((timestamp + 5000) / 10000);

  char utc_us_buf[32];
  std::snprintf(utc_us_buf, sizeof(utc_us_buf), "%lld", (long long)utcCreationUs);

  av_dict_set(&pFormatCtx->metadata, "creation_time", isoCreationTime.c_str(), 0);
  av_dict_set(&pFormatCtx->metadata, "com.crewtimer.first_utc_us", utc_us_buf, 0);

  return "";
}

void FFVideoRecorder::stampStreamMetadata()
{
  if (!video_st)
    return;

  if (!isoCreationTime.empty())
  {
    av_dict_set(&video_st->metadata, "creation_time", isoCreationTime.c_str(), 0);
  }
  if (utcCreationUs != 0)
  {
    char utc_us_buf[32];
    std::snprintf(utc_us_buf, sizeof(utc_us_buf), "%lld", (long long)utcCreationUs);
    av_dict_set(&video_st->metadata, "com.crewtimer.first_utc_us", utc_us_buf, 0);
  }
}

std::string FFVideoRecorder::initializeEncoderPipeline()
{
  if (!pFormatCtx)
  {
    return "Error: format context not initialized";
  }

  const AVOutputFormat *oformat = pFormatCtx->oformat;

  std::vector<std::string> codec_names = {"h264_v4l2m2m",      // Raspberry Pi 4
                                          "h264_videotoolbox", // Apple
                                          "h264_nvenc",        // NVIDIA
                                          "h264_qsv",          // Intel
                                          "h264_amf",          // AMD
                                          "libx264",           // SOFTWARE
                                          "libx264rgb"};

  const AVCodec *codec = nullptr;
  for (const auto &name : codec_names)
  {
    codec = avcodec_find_encoder_by_name(name.c_str());
    if (codec)
      break;
  }

  if (!codec)
  {
    codec = avcodec_find_encoder(oformat->video_codec);
  }
  if (!codec)
  {
    auto msg = "Error: Codec for mp4 not found";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }

  if (codecName.empty())
  {
    codecName = codec->name;
    SystemEventQueue::push("ffmpeg", "Using codec " + codecName);
  }

  video_st = avformat_new_stream(pFormatCtx, NULL);
  if (!video_st)
  {
    auto msg = "Error: Could not allocate video stream";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }
  video_st->id = pFormatCtx->nb_streams - 1;

  pCodecCtx = avcodec_alloc_context3(codec);
  if (!pCodecCtx)
  {
    auto msg = "Error: Could not allocate video codec context";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }

  if (pendingFrameRate.num == 0 || pendingFrameRate.den == 0)
  {
    pendingFrameRate = av_make_q(30, 1);
  }
  if (pendingTimeBase.num == 0 || pendingTimeBase.den == 0)
  {
    pendingTimeBase = av_inv_q(pendingFrameRate);
  }

  pCodecCtx->bit_rate = 6000000;
  pCodecCtx->width = pendingWidth;
  pCodecCtx->height = pendingHeight;
  pCodecCtx->framerate = pendingFrameRate;
  pCodecCtx->time_base = pendingTimeBase;
  pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
  pCodecCtx->max_b_frames = 0;
  pCodecCtx->thread_count = 0;
  pCodecCtx->gop_size = getKeyFrameInterval();
  if (std::string("h264_videotoolbox") == codec->name)
  {
    pCodecCtx->qmin = -1;
    pCodecCtx->qmax = -1;
  }
  video_st->time_base = pCodecCtx->time_base;

  AVDictionary *codec_options = NULL;
  av_dict_set(&codec_options, "preset", "medium", 0);

  if (avcodec_open2(pCodecCtx, codec, &codec_options) < 0)
  {
    av_dict_free(&codec_options);
    auto msg = "Error: Could not open codec using preset medium";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }
  av_dict_free(&codec_options);

  if (avcodec_parameters_from_context(video_st->codecpar, pCodecCtx) < 0)
  {
    const auto msg = "Error: Could not copy codec parameters";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }

  stampStreamMetadata();

  if (muxer_has_opt(pFormatCtx, "write_prft"))
  {
    av_opt_set_int(pFormatCtx->priv_data, "write_prft", 1, 0);
    av_opt_set(pFormatCtx->priv_data, "prft", "wallclock", 0);
  }

  AVDictionary *opts = nullptr;
  av_dict_set(&opts, "movflags", "use_metadata_tags", 0);
  int err = avformat_write_header(pFormatCtx, &opts);
  av_dict_free(&opts);
  if (err < 0)
  {
    auto msg = "Error: Cannot write mp4 header";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }
  headerWritten = true;

  pFrame = av_frame_alloc();
  if (!pFrame)
  {
    return "Error: Could not allocate video frame";
  }
  pFrame->format = pCodecCtx->pix_fmt;
  pFrame->width = pCodecCtx->width;
  pFrame->height = pCodecCtx->height;
  pFrame->color_range = AVCOL_RANGE_MPEG;
  if (av_frame_get_buffer(pFrame, 0) < 0)
  {
    return "Error: Could not allocate the video frame data";
  }

  if (!pkt)
  {
    pkt = av_packet_alloc();
    if (!pkt)
    {
      return "Error: Unable to allocate AVPacket";
    }
  }

  sws_ctx = nullptr;
  streamInitialized = true;
  usePacketInput = false;

  return "";
}

std::string FFVideoRecorder::initializePacketPipeline(const FramePtr &firstPacket)
{
  if (!pFormatCtx)
  {
    return "Error: format context not initialized";
  }
  if (!firstPacket || !firstPacket->hasEncodedData())
  {
    return "Error: No encoded packet supplied for initialization";
  }

  video_st = avformat_new_stream(pFormatCtx, NULL);
  if (!video_st)
  {
    return "Error: Could not allocate video stream";
  }
  video_st->id = pFormatCtx->nb_streams - 1;

  auto &encoded = firstPacket->encodedPacket;
  AVCodecParameters *codecpar = video_st->codecpar;
  codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  codecpar->codec_id = static_cast<AVCodecID>(encoded.codecId != 0 ? encoded.codecId : AV_CODEC_ID_H264);
  codecpar->width = encoded.width > 0 ? encoded.width : pendingWidth;
  codecpar->height = encoded.height > 0 ? encoded.height : pendingHeight;
  codecpar->format = -1;

  if (!encoded.extradata.empty())
  {
    codecpar->extradata = (uint8_t *)av_mallocz(encoded.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!codecpar->extradata)
    {
      return "Error: Unable to allocate codec extradata";
    }
    codecpar->extradata_size = static_cast<int>(encoded.extradata.size());
    memcpy(codecpar->extradata, encoded.extradata.data(), encoded.extradata.size());
  }

  if (encoded.timeBaseNum != 0 && encoded.timeBaseDen != 0)
  {
    passthroughTimeBase = AVRational{encoded.timeBaseNum, encoded.timeBaseDen};
  }
  else if (pendingTimeBase.num != 0 && pendingTimeBase.den != 0)
  {
    passthroughTimeBase = pendingTimeBase;
  }
  else
  {
    passthroughTimeBase = AVRational{1, 90000};
  }
  video_st->time_base = passthroughTimeBase;

  if (encoded.avgFrameRateNum != 0 && encoded.avgFrameRateDen != 0)
  {
    passthroughFrameRate = AVRational{encoded.avgFrameRateNum, encoded.avgFrameRateDen};
  }
  else if (pendingFrameRate.num != 0 && pendingFrameRate.den != 0)
  {
    passthroughFrameRate = pendingFrameRate;
  }
  else
  {
    passthroughFrameRate = AVRational{30, 1};
  }
  video_st->avg_frame_rate = passthroughFrameRate;

  stampStreamMetadata();

  if (muxer_has_opt(pFormatCtx, "write_prft"))
  {
    av_opt_set_int(pFormatCtx->priv_data, "write_prft", 1, 0);
    av_opt_set(pFormatCtx->priv_data, "prft", "wallclock", 0);
  }

  AVDictionary *opts = nullptr;
  av_dict_set(&opts, "movflags", "use_metadata_tags", 0);
  int err = avformat_write_header(pFormatCtx, &opts);
  av_dict_free(&opts);
  if (err < 0)
  {
    auto msg = "Error: Cannot write mp4 header";
    SystemEventQueue::push("ffmpeg", msg);
    return msg;
  }
  headerWritten = true;

  if (!pkt)
  {
    pkt = av_packet_alloc();
    if (!pkt)
    {
      return "Error: Unable to allocate AVPacket";
    }
  }

  streamInitialized = true;
  usePacketInput = true;
  return "";
}

std::string FFVideoRecorder::encodeAndWriteFrame(FramePtr video_frame)
{
  if (!video_frame || !pCodecCtx)
  {
    return "";
  }

  int inLinesize[1] = {video_frame->stride};
  if (sws_ctx == nullptr)
  {
    auto src_fmt = AV_PIX_FMT_UYVY422;
    switch (video_frame->pixelFormat)
    {
    case Frame::PixelFormat::RGBX:
      src_fmt = AV_PIX_FMT_RGBA;
      inLinesize[0] = {4 * video_frame->xres};
      break;
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
        pCodecCtx->height, pCodecCtx->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
  }

  uint8_t *inData[1] = {video_frame->data};
  sws_scale(sws_ctx, inData, inLinesize, 0, pCodecCtx->height, pFrame->data, pFrame->linesize);

  pFrame->pts = av_rescale_q(frame_index++, pCodecCtx->time_base, video_st->time_base);

  std::string errorMsg = "";
  if (avcodec_send_frame(pCodecCtx, pFrame) < 0)
  {
    errorMsg = "Error: Cannot send a frame for encoding";
    SystemEventQueue::push("ffmpeg", errorMsg);
  }

  while (true)
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
      break;
    }

    if (av_write_frame(pFormatCtx, pkt) < 0)
    {
      if (errorMsg.empty())
      {
        errorMsg = "Error: Cannot write video frame";
        SystemEventQueue::push("ffmpeg", errorMsg);
      }
      av_packet_unref(pkt);
      break;
    }
    av_packet_unref(pkt);
  }

  return errorMsg;
}

std::string FFVideoRecorder::writeEncodedPacket(FramePtr video_frame)
{
  if (!video_frame || !video_frame->hasEncodedData() || !pkt || !video_st)
  {
    return "";
  }

  auto &encoded = video_frame->encodedPacket;
  if (encoded.data.empty())
  {
    return "";
  }

  if (av_new_packet(pkt, static_cast<int>(encoded.data.size())) < 0)
  {
    return "Error: Unable to allocate packet buffer";
  }
  memcpy(pkt->data, encoded.data.data(), encoded.data.size());

  pkt->pts = encoded.pts;
  pkt->dts = encoded.dts;
  pkt->duration = encoded.duration;
  pkt->flags = encoded.keyFrame ? AV_PKT_FLAG_KEY : 0;
  pkt->stream_index = video_st->index;

  AVRational srcTimeBase = passthroughTimeBase;
  if (encoded.timeBaseNum != 0 && encoded.timeBaseDen != 0)
  {
    srcTimeBase = AVRational{encoded.timeBaseNum, encoded.timeBaseDen};
  }
  if (srcTimeBase.num != 0 && srcTimeBase.den != 0)
  {
    av_packet_rescale_ts(pkt, srcTimeBase, video_st->time_base);
  }

  if (av_interleaved_write_frame(pFormatCtx, pkt) < 0)
  {
    av_packet_unref(pkt);
    return "Error: Cannot write encoded packet";
  }

  av_packet_unref(pkt);
  return "";
}

std::string FFVideoRecorder::openVideoStream(std::string directory, std::string filename,
                                             int width, int height, float fps, uint64_t timestamp)
{
  resetState();

  pendingWidth = width;
  pendingHeight = height;
  pendingFps = fps;

  double fpsValue = fps > 0.f ? fps : 30.0;
  int fpsNum = static_cast<int>(fpsValue * 1000.0 + 0.5);
  if (fpsNum <= 0)
    fpsNum = 30000;
  pendingFrameRate = AVRational{fpsNum, 1000};
  pendingTimeBase = av_inv_q(pendingFrameRate);

  std::string initError = initializeContainer(directory, filename, timestamp);
  return initError;
}

std::string FFVideoRecorder::writeVideoFrame(FramePtr video_frame)
{
  if (!pFormatCtx)
  {
    return "Error: Recorder not initialized";
  }
  if (!video_frame)
  {
    return "";
  }
  if (video_frame->frameType == Frame::FrameType::SOURCE_DISCONNECTED)
  {
    return "";
  }

  if (!streamInitialized)
  {
    bool wantPacketMode = video_frame->hasEncodedData();
    std::string initErr = wantPacketMode ? initializePacketPipeline(video_frame)
                                         : initializeEncoderPipeline();
    if (!initErr.empty())
    {
      return initErr;
    }
  }

  if (usePacketInput)
  {
    if (!video_frame->hasEncodedData())
    {
      return "Error: Expected encoded packet input";
    }
    return writeEncodedPacket(video_frame);
  }

  if (video_frame->frameType != Frame::FrameType::VIDEO)
  {
    return "";
  }

  return encodeAndWriteFrame(video_frame);
}

std::string FFVideoRecorder::stop()
{
  std::string retval = "";

  if (!usePacketInput && pCodecCtx && pkt)
  {
    if (avcodec_send_frame(pCodecCtx, NULL) < 0)
    {
      auto msg = "Error: send frame to encoder failed";
      SystemEventQueue::push("ffmpeg", msg);
      retval = msg;
    }

    while (true)
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
      if (av_write_frame(pFormatCtx, pkt) < 0)
      {
        av_packet_unref(pkt);
        auto msg = "Error: Cannot write video frame";
        SystemEventQueue::push("ffmpeg", msg);
        retval = msg;
        break;
      }
      av_packet_unref(pkt);
    }
  }

  if (headerWritten && pFormatCtx)
  {
    av_write_trailer(pFormatCtx);
  }

  if (pFormatCtx && !(pFormatCtx->oformat->flags & AVFMT_NOFILE))
  {
    avio_closep(&pFormatCtx->pb);
  }
  if (pFormatCtx)
  {
    avformat_free_context(pFormatCtx);
    pFormatCtx = nullptr;
  }

  resetState();

  if (!tmpFile.empty())
  {
    if (std::rename(tmpFile.c_str(), outputFile.c_str()) != 0)
    {
      auto msg = "Error: Cannot rename file";
      SystemEventQueue::push("ffmpeg", msg);
      retval = msg;
    }
    tmpFile.clear();
  }

  return retval;
}

std::shared_ptr<VideoRecorder> createFfmpegRecorder()
{
  return std::shared_ptr<FFVideoRecorder>(new FFVideoRecorder());
}
