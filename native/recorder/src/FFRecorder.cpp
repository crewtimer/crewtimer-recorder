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
    frame_index = 0;
    outputFile = filename + ".mp4";
    tmpFile = directory + "/" + "tmp-" + outputFile;
    outputFile = directory + "/" + outputFile;
    // std::cout << "Creating file '" << outputFile << "'" << std::endl;

    av_log_set_level(AV_LOG_ERROR);
    // av_log_set_level(AV_LOG_INFO);

    /* allocate the output media context */
    avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, tmpFile.c_str());
    if (!pFormatCtx)
    {
      const auto msg = "Could not deduce output format from file extension: "
                       "using MPEG.";
      SystemEventQueue::push("ffmpeg", msg);
      avformat_alloc_output_context2(&pFormatCtx, NULL, "mpeg",
                                     tmpFile.c_str());
    }
    if (!pFormatCtx)
    {
      auto msg = "Could not allocate format context";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }

    const AVOutputFormat *oformat = pFormatCtx->oformat;

    // Codec names in hardware accelerated preferred order, platform-specific
#if defined(__APPLE__)
    std::vector<std::string> codec_names = {
        "h264_videotoolbox", // Apple Silicon / Intel Mac hardware
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
        "h264_v4l2m2m", // Raspberry Pi / V4L2
        "h264_nvenc",   // NVIDIA on Linux
        "h264_qsv",     // Intel on Linux
        "libx264",
        "libx264rgb"};
#endif

    const AVCodec *codec = nullptr;
    for (const auto &name : codec_names)
    {
      codec = avcodec_find_encoder_by_name(
          name.c_str()); // Convert std::string to const char*
      if (codec)
      {
        break;
      }
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

    //??c->codec_id = codec_id;

    // Set your codec parameters here
    pCodecCtx->bit_rate = 6000000;
    pCodecCtx->width = width;
    pCodecCtx->height = height;
    pCodecCtx->framerate = av_make_q(static_cast<int>(fps), 1);
    pCodecCtx->time_base = av_make_q(1, int(fps));
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    pCodecCtx->max_b_frames = 0;
    pCodecCtx->thread_count = 0; // let codec decide
    pCodecCtx->gop_size = getKeyFrameInterval();
    if (std::string("h264_videotoolbox") == codec->name)
    {
      pCodecCtx->qmin = -1;
      pCodecCtx->qmax = -1;
    }
    video_st->time_base = pCodecCtx->time_base; // Use same timebase for both

    /* Some formats want stream headers to be separate. */
    // if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    //   c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *codec_options = NULL;
    if (codecName == "libx264" || codecName == "libx264rgb")
    {
      // 'preset' controls compression speed; slower = more compression
      av_dict_set(&codec_options, "preset", "medium", 0);
    }
    else if (codecName == "h264_nvenc")
    {
      av_dict_set(&codec_options, "preset", "p4", 0); // balanced quality/speed
      av_dict_set(&codec_options, "tune", "hq", 0);
    }
    else if (codecName == "h264_qsv")
    {
      av_dict_set(&codec_options, "preset", "medium", 0);
    }
    // h264_videotoolbox, h264_amf, h264_mf: use encoder defaults

    if (avcodec_open2(pCodecCtx, codec, &codec_options) < 0)
    {
      auto msg = "Error: Could not open codec " + codecName;
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }

    av_dict_free(&codec_options);

    if (avcodec_parameters_from_context(video_st->codecpar, pCodecCtx) < 0)
    {
      const auto msg = "Error: Could not copy codec parameters";
      SystemEventQueue::push("ffmpeg", msg);
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

    // Optional: PRFT if your build supports it
    if (muxer_has_opt(pFormatCtx, "write_prft"))
    {
      av_opt_set_int(pFormatCtx->priv_data, "write_prft", 1, 0);
      av_opt_set(pFormatCtx->priv_data, "prft", "wallclock", 0);
    }
    // Prepare values
    long long utc_us = (timestamp + 5) / 10; // 100ns to microseconds
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

    return "";
  }

  std::string writeVideoFrame(FramePtr video_frame)
  {
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
      for (int y = 0; y < h / 2; y++)
        memcpy(pFrame->data[1] + y * pFrame->linesize[1], srcU + y * (w / 2), w / 2);
      for (int y = 0; y < h / 2; y++)
        memcpy(pFrame->data[2] + y * pFrame->linesize[2], srcV + y * (w / 2), w / 2);
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

    pFrame->pts =
        av_rescale_q(frame_index++, pCodecCtx->time_base, video_st->time_base);

    std::string errorMsg = "";
    if (avcodec_send_frame(pCodecCtx, pFrame) < 0)
    {
      errorMsg = "Error: Cannot send a frame for encoding";
      SystemEventQueue::push("ffmpeg", errorMsg);
      // Do not return yet; still drain packets below
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

      if (av_write_frame(pFormatCtx, pkt) < 0)
      {
        if (errorMsg.empty())
        {
          errorMsg = "Error: Cannot write video frame";
          SystemEventQueue::push("ffmpeg", errorMsg);
        }
        // Continue draining packets even on error
        av_packet_unref(pkt);
        break;
      }
      av_packet_unref(pkt);
    }

    return errorMsg;
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
      // Properly close the output file
      av_write_trailer(pFormatCtx);

      if (!(pFormatCtx->oformat->flags & AVFMT_NOFILE))
      {
        avio_closep(&pFormatCtx->pb);
      }
    }

    if (pCodecCtx)
    {
      avcodec_free_context(&pCodecCtx);
    }
    if (pFormatCtx)
    {
      avformat_free_context(pFormatCtx);
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
    pFormatCtx = nullptr;
    pFrame = nullptr;
    pkt = nullptr;
    sws_ctx = nullptr;
    video_st = nullptr;

    if (tmpFile.length() > 0)
    {
      // Attempt to rename the file
      if (std::rename(tmpFile.c_str(), outputFile.c_str()) == 0)
      {
        // std::cout << "File successfully renamed from " << tmpFile << " to "
        //           << outputFile << std::endl;
      }
      else
      {
        // If renaming failed, print an error message
        auto msg = "Error: Cannot rename file";
        SystemEventQueue::push("ffmpeg", msg);
        retval = msg;
      }
      tmpFile = "";
    }
    return retval;
  }
  ~FFVideoRecorder() {}
};

std::shared_ptr<VideoRecorder> createFfmpegRecorder()
{
  return std::shared_ptr<FFVideoRecorder>(new FFVideoRecorder());
}
