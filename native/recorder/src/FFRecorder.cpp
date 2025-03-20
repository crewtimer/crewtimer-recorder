#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
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
  std::string openVideoStream(std::string directory, std::string filename,
                              int width, int height, float fps)
  {
    frame_index = 0;
    outputFile = filename + ".mp4";
    tmpFile = directory + "/" + "tmp-" + outputFile;
    outputFile = directory + "/" + outputFile;
    // std::cout << "Creating file '" << outputFile << "'" << std::endl;

    // av_log_set_level(AV_LOG_DEBUG);
    av_log_set_level(AV_LOG_INFO);

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

    // Codec names in hardware accelerated preferred order
    std::vector<std::string> codec_names = {"h264_v4l2m2m",      // Raspberry Pi 4
                                            "h264_videotoolbox", // Apple
                                            "h264_nvenc",        // NVIDIA
                                            "h264_qsv",          // Intel
                                            "h264_amf"           // AMD
                                            "libx264",           // SOFTWARE
                                            "libx264rgb"};

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
    pCodecCtx->thread_count = 2;
    pCodecCtx->gop_size = 12;
    if (std::string("h264_videotoolbox") == codec->name)
    {
      pCodecCtx->qmin = -1;
      pCodecCtx->qmax = -1;
    }

    /* Some formats want stream headers to be separate. */
    // if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    //   c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *codec_options = NULL;
    // 'profile' sets the feature set available to a decoder and thus limits
    // encoding options.  'preset' controls conmpression speed.  Slower speed
    // means more compression generally.
    av_dict_set(&codec_options, "preset", "medium", 0);
    // av_dict_set(&codec_options, "profile", "high", 0); // not found on
    // windows av_dict_set(&codec_options, "preset", "slow", 0); // 80% cpu
    // 90MB/min av_dict_set(&codec_options, "profile", "main", 0);

    if (avcodec_open2(pCodecCtx, codec, &codec_options) < 0)
    {
      auto msg = "Error: Could not open codec using preset medium";
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

    if (avformat_write_header(pFormatCtx, NULL) < 0)
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
        // UYVY422 format, where each pixel consists
        // of two chrominance (U and V) values and two
        // luminance (Y) values.
        src_fmt = AV_PIX_FMT_UYVY422;
        break;
      }
      sws_ctx = sws_getContext(
          pCodecCtx->width, pCodecCtx->height, src_fmt, pCodecCtx->width,
          pCodecCtx->height,
          pCodecCtx->pix_fmt, // AV_PIX_FMT_YUV420P, // YUV 4:2:0 for H.264
          SWS_BICUBIC, NULL, NULL, NULL);
    }

    // Convert the image format from receiver format to the codec's format

    uint8_t *inData[1] = {video_frame->data};
    sws_scale(sws_ctx, inData, inLinesize, 0, pCodecCtx->height, pFrame->data,
              pFrame->linesize);

    pFrame->pts = frame_index++;
    pFrame->pts =
        av_rescale_q(frame_index, pCodecCtx->time_base, video_st->time_base);

    if (avcodec_send_frame(pCodecCtx, pFrame) < 0)
    {
      auto msg = "Error: Cannot send a frame for encoding";
      SystemEventQueue::push("ffmpeg", msg);
      return msg;
    }

    while (1)
    {
      int ret = avcodec_receive_packet(pCodecCtx, pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      else if (ret < 0)
      {
        auto msg = "Error: encoding error";
        SystemEventQueue::push("ffmpeg", msg);
        return msg;
      }

      if (av_interleaved_write_frame(pFormatCtx, pkt) < 0)
      {
        auto msg = "Error: Cannot write video frame";
        SystemEventQueue::push("ffmpeg", msg);
        return msg;
      }
      av_packet_unref(pkt);
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
          break;
        else if (ret < 0)
        {
          auto msg = "Error: avcodec receive packet fail";
          SystemEventQueue::push("ffmpeg", msg);
          retval = msg;
          break;
        }
        if (av_interleaved_write_frame(pFormatCtx, pkt) < 0)
        {
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
