#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include "VideoRecorder.hpp"

class FFVideoRecorder : public VideoRecorder {
  int frame_index;
  AVFrame *pFrame;
  AVPacket *pkt;
  struct SwsContext *sws_ctx;
  AVFormatContext *pFormatCtx;
  AVCodecContext *pCodecCtx;
  AVStream *video_st;
  std::string outputFile;
  std::string tmpFile;

public:
  FFVideoRecorder() {
    // Get the version of the libavutil library
    unsigned version = avutil_version();

    std::cout << "libavutil version: " << version << std::endl;
    std::cout << "Major: " << AV_VERSION_MAJOR(version) << std::endl;
    std::cout << "Minor: " << AV_VERSION_MINOR(version) << std::endl;
    std::cout << "Micro: " << AV_VERSION_MICRO(version) << std::endl;

    // For a textual representation
    std::cout << "libavutil version (textual): " << av_version_info()
              << std::endl;
  }
  std::string openVideoStream(std::string directory, std::string filename,
                              int width, int height, float fps) {
    frame_index = 0;
    outputFile = filename + ".mp4";
    tmpFile = directory + "/" + "tmp-" + outputFile;
    outputFile = directory + "/" + outputFile;
    // std::cout << "Creating file '" << outputFile << "'" << std::endl;

    // av_log_set_level(AV_LOG_DEBUG);
    av_log_set_level(AV_LOG_INFO);

    /* allocate the output media context */
    avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, tmpFile.c_str());
    if (!pFormatCtx) {
      std::cerr
          << "Could not deduce output format from file extension: using MPEG."
          << std::endl;
      avformat_alloc_output_context2(&pFormatCtx, NULL, "mpeg",
                                     tmpFile.c_str());
    }
    if (!pFormatCtx) {
      auto msg = "Could not allocate format context";
      std::cerr << msg << std::endl;
      return msg;
    }

    const AVOutputFormat *oformat = pFormatCtx->oformat;

    // Codec names in hardware accelerated preferred order
    std::vector<std::string> codec_names = {"h264_v4l2m2m", // Raspberry Pi 4
                                            "h264_videotoolbox", // Apple
                                            "h264_nvenc",        // NVIDIA
                                            "h264_qsv",          // Intel
                                            "h264_amf"           // AMD
                                            "libx264",           // SOFTWARE
                                            "libx264rgb"};

    const AVCodec *codec = nullptr;
    for (const auto &name : codec_names) {
      codec = avcodec_find_encoder_by_name(
          name.c_str()); // Convert std::string to const char*
      if (codec) {
        std::cout << "Found " << name << std::endl;
        break;
      }
    }

    if (!codec) {
      codec = avcodec_find_encoder(oformat->video_codec);
      if (codec)
        std::cout << "Using codec " << codec->name << std::endl;
    }
    if (!codec) {
      auto msg = "Codec for mp4 not found";
      std::cerr << msg << std::endl;
      return msg;
    }

    video_st = avformat_new_stream(pFormatCtx, NULL);
    if (!video_st) {
      auto msg = "Could not allocate video stream";
      std::cerr << msg << std::endl;
      return msg;
    }
    video_st->id = pFormatCtx->nb_streams - 1;

    pCodecCtx = avcodec_alloc_context3(codec);
    if (!pCodecCtx) {
      auto msg = "Could not allocate video codec context";
      std::cerr << msg << std::endl;
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

    /* Some formats want stream headers to be separate. */
    // if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    //   c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *codec_options = NULL;
    // 'profile' sets the feature set available to a decoder and thus limits
    // encoding options.  'preset' controls conmpression speed.  Slower speed
    // means more compression generally.
    av_dict_set(&codec_options, "preset", "medium", 0); // 120% cpu 60MB/min
    // av_dict_set(&codec_options, "profile", "high", 0);
    // av_dict_set(&codec_options, "preset", "slow", 0); // 80% cpu 90MB/min
    // av_dict_set(&codec_options, "profile", "main", 0);

    if (avcodec_open2(pCodecCtx, codec, &codec_options) < 0) {
      auto msg = "Could not open codec";
      std::cerr << msg << std::endl;
      return msg;
    }

    av_dict_free(&codec_options);

    if (avcodec_parameters_from_context(video_st->codecpar, pCodecCtx) < 0) {
      std::cerr << "Could not copy codec parameters" << std::endl;
    }

    if (!(pFormatCtx->oformat->flags & AVFMT_NOFILE)) {
      if (avio_open(&pFormatCtx->pb, tmpFile.c_str(), AVIO_FLAG_WRITE) < 0) {
        auto msg = "Could not open " + tmpFile;
        std::cerr << msg << std::endl;
        return msg;
      }
    }

    if (avformat_write_header(pFormatCtx, NULL) < 0) {
      auto msg = "Error occurred when opening output file";
      std::cerr << msg << std::endl;
      return msg;
    }

    pFrame = av_frame_alloc();
    if (!pFrame) {
      auto msg = "Could not allocate video frame";
      std::cerr << msg << std::endl;
      return msg;
    }
    pFrame->format = pCodecCtx->pix_fmt;
    pFrame->width = pCodecCtx->width;
    pFrame->height = pCodecCtx->height;
    pFrame->color_range = AVCOL_RANGE_MPEG;
    if (av_frame_get_buffer(pFrame, 0) < 0) {
      auto msg = "Could not allocate the video frame data";
      std::cerr << msg << std::endl;
      return msg;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
      auto msg = "Error allocating AVPacket";
      std::cerr << msg << std::endl;
      return msg;
    }
    pkt->data = NULL;
    pkt->size = 0;

#ifdef NDI_BGRX
    sws_ctx = sws_getContext(
        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_BGRA, pCodecCtx->width,
        pCodecCtx->height, pCodecCtx->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
#else
    sws_ctx =
        sws_getContext(pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_UYVY422,
                       pCodecCtx->width, pCodecCtx->height,
                       AV_PIX_FMT_YUV420P, // YUV 4:2:0 for H.264
                       SWS_BICUBIC, NULL, NULL, NULL);
#endif

    return "";
  }

  std::string writeVideoFrame(FramePtr video_frame) {

    // Convert the image format from NDI's format to the codec's format
#ifdef NDI_BGRX
    uint8_t *inData[1] = {video_frame->data};
    int inLinesize[1] = {4 * video_frame->xres}; // BGRA
    sws_scale(sws_ctx, inData, inLinesize, 0, pCodecCtx->height, pFrame->data,
              pFrame->linesize);
#else
    uint8_t *inData[1] = {video_frame->data};
    int inLinesize[1] = {2 * video_frame->xres}; // 2 bytes per pixel for UYVY
    sws_scale(sws_ctx, inData, inLinesize, 0, pCodecCtx->height, pFrame->data,
              pFrame->linesize);
#endif
    pFrame->pts = frame_index++;
    pFrame->pts =
        av_rescale_q(frame_index, pCodecCtx->time_base, video_st->time_base);

    if (avcodec_send_frame(pCodecCtx, pFrame) < 0) {
      auto msg = "Error sending a frame for encoding";
      std::cerr << msg << std::endl;
      return msg;
    }

    while (1) {
      int ret = avcodec_receive_packet(pCodecCtx, pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      else if (ret < 0) {
        auto msg = "Error during encoding";
        std::cerr << msg << std::endl;
        return msg;
      }

      if (av_interleaved_write_frame(pFormatCtx, pkt) < 0) {
        auto msg = "Error while writing video frame.";
        std::cerr << msg << std::endl;
        return msg;
      }
      av_packet_unref(pkt);
    }

    return "";
  }

  std::string stop() {
    if (video_st == nullptr) {
      return "";
    }
    // Flush the encoder
    if (avcodec_send_frame(pCodecCtx, NULL) < 0) {
      auto msg = "Error sending a frame for encoding";
      std::cerr << msg << std::endl;
      return msg;
    }

    // If pkt is null, it is uninitialized so we haven't read anything yet.
    if (pkt) {

      while (1) {
        int ret = avcodec_receive_packet(pCodecCtx, pkt);
        if (ret == AVERROR_EOF)
          break;
        else if (ret < 0) {
          auto msg = "Error during encoding";
          std::cerr << msg << std::endl;
          return msg;
        }
        if (av_interleaved_write_frame(pFormatCtx, pkt) < 0) {
          auto msg = "Error while writing video frame.";
          std::cerr << msg << std::endl;
          return msg;
        }
        av_packet_unref(pkt);
      }
      // Properly close the output file
      av_write_trailer(pFormatCtx);

      if (!(pFormatCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&pFormatCtx->pb);
      }
    }

    avcodec_free_context(&pCodecCtx);
    avformat_free_context(pFormatCtx);
    av_frame_free(&pFrame);
    av_packet_free(&pkt);
    sws_freeContext(sws_ctx);
    video_st = nullptr;

    // Attempt to rename the file
    if (std::rename(tmpFile.c_str(), outputFile.c_str()) == 0) {
      // std::cout << "File successfully renamed from " << tmpFile << " to "
      //           << outputFile << std::endl;
    } else {
      // If renaming failed, print an error message
      auto msg = "Error renaming file";
      std::cerr << msg << std::endl;
      return msg;
    }
    return "";
  }
  ~FFVideoRecorder() {}
};

std::shared_ptr<VideoRecorder> createFfmpegRecorder() {
  return std::shared_ptr<FFVideoRecorder>(new FFVideoRecorder());
}
