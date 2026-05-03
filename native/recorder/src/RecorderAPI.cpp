#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <napi.h>
#include <node.h>
#include <sstream>
#include <streambuf>

#ifdef __APPLE__
extern "C" void triggerMacOSLocalNetworkPermission();
#endif

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include "Message.hpp"
#include "SystemEventQueue.hpp"
#include "VideoController.hpp"
#include "visca/IViscaTcpClient.hpp"
#include "event/NativeEvent.hpp"
#include "opencv/FocusScore.hpp"

using json = nlohmann::json;
std::shared_ptr<VideoController> videoController;
std::unique_ptr<IViscaTcpClient> viscaClient;

// Encode a raw video frame as a JPEG using FFmpeg's MJPEG encoder.
// Works directly on YUV420P or UYVY422 input — no intermediate BGR step.
static std::vector<uint8_t> encodeFrameAsJpeg(const FramePtr &videoFrame, int quality)
{
  std::vector<uint8_t> result;

  const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  if (!codec)
    return result;

  AVCodecContext *ctx = avcodec_alloc_context3(codec);
  if (!ctx)
    return result;

  ctx->width = videoFrame->xres;
  ctx->height = videoFrame->yres;
  ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
  ctx->time_base = AVRational{1, 25};
  ctx->flags |= AV_CODEC_FLAG_QSCALE;
  // Map quality 0-100 → QP 31-2 (lower QP = better)
  ctx->global_quality = FF_QP2LAMBDA * std::max(2, (100 - quality) / 3);

  if (avcodec_open2(ctx, codec, nullptr) < 0)
  {
    avcodec_free_context(&ctx);
    return result;
  }

  AVFrame *frame = av_frame_alloc();
  frame->format = AV_PIX_FMT_YUVJ420P;
  frame->width = videoFrame->xres;
  frame->height = videoFrame->yres;
  frame->pts = 0;
  av_frame_get_buffer(frame, 32);

  // Build source plane/stride arrays for swscale
  AVPixelFormat srcFmt;
  const uint8_t *srcPlanes[4] = {};
  int srcStrides[4] = {};

  if (videoFrame->pixelFormat == Frame::PixelFormat::YUV420P)
  {
    srcFmt = AV_PIX_FMT_YUV420P;
    srcPlanes[0] = videoFrame->data;
    srcPlanes[1] = videoFrame->data + videoFrame->xres * videoFrame->yres;
    srcPlanes[2] = videoFrame->data + videoFrame->xres * videoFrame->yres * 5 / 4;
    srcStrides[0] = videoFrame->xres;
    srcStrides[1] = videoFrame->xres / 2;
    srcStrides[2] = videoFrame->xres / 2;
  }
  else // UYVY422
  {
    srcFmt = AV_PIX_FMT_UYVY422;
    srcPlanes[0] = videoFrame->data;
    srcStrides[0] = videoFrame->stride;
  }

  SwsContext *sws = sws_getContext(
      videoFrame->xres, videoFrame->yres, srcFmt,
      videoFrame->xres, videoFrame->yres, AV_PIX_FMT_YUVJ420P,
      SWS_BILINEAR, nullptr, nullptr, nullptr);

  if (sws)
  {
    sws_scale(sws, srcPlanes, srcStrides, 0, videoFrame->yres, frame->data, frame->linesize);
    sws_freeContext(sws);
  }

  AVPacket *pkt = av_packet_alloc();
  if (avcodec_send_frame(ctx, frame) == 0 && avcodec_receive_packet(ctx, pkt) == 0)
    result.assign(pkt->data, pkt->data + pkt->size);

  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&ctx);
  return result;
}

Napi::Value
convertEventsToJS(const Napi::Env &env,
                  const std::vector<std::shared_ptr<SystemEvent>> &eventList)
{
  Napi::Array jsArray = Napi::Array::New(env);

  for (size_t i = 0; i < eventList.size(); ++i)
  {
    Napi::Object jsEvent = Napi::Object::New(env);
    jsEvent.Set("tsMilli", Napi::Number::New(env, eventList[i]->tsMilli));
    jsEvent.Set("subsystem", Napi::String::New(env, eventList[i]->subsystem));
    jsEvent.Set("message", Napi::String::New(env, eventList[i]->message));

    jsArray[i] = jsEvent;
  }

  return jsArray;
}

// Define a destructor to free uint8_t buffers
void FinalizeBuffer(Napi::Env env, void *data)
{
  // Clean up memory if necessary
  delete[] static_cast<uint8_t *>(data);
}

auto viscaStatusLogger = [](const std::string &msg)
{
  std::cout << "[STATUS] " << msg << std::endl;
  json config = {{"msg", msg}};
  sendMessageToRenderer("visca-status", std::make_shared<json>(config));
};

auto viscaStateLogger = [](const std::string &msg)
{
  std::cout << "[VISCA STATE] " << msg << std::endl;
  json config = {{"state", msg}};
  sendMessageToRenderer("visca-state", std::make_shared<json>(config));
};

static std::string getNapiStringField(
    Napi::Object &obj,
    const std::string &fieldName,
    const std::string &defaultValue = "")
{
  if (obj.Has(fieldName.c_str()))
  {
    return obj.Get(fieldName.c_str()).As<Napi::String>().Utf8Value();
  }
  return defaultValue;
}

// global for focus processing
static auto cropRect = FrameProcessor::FRectangle{0, 0, 0, 0};
static FrameProcessor::Guide guide;

// Struct for focus area configuration (replaces individual static variables)
struct FocusAreaConfig
{
  double xPct = 0;
  double yPct = 0.5;
  double sizePct = 0.2;
  bool enabled = true;
  void setFromNapi(Napi::Object &focus)
  {
    if (focus.Has("enabled"))
    {
      enabled = focus.Get("enabled").As<Napi::Boolean>();
    }
    if (focus.Has("xPct"))
    {
      xPct = focus.Get("xPct").As<Napi::Number>();
    }
    if (focus.Has("yPct"))
    {
      yPct = focus.Get("yPct").As<Napi::Number>();
    }
    if (focus.Has("sizePct"))
    {
      sizePct = focus.Get("sizePct").As<Napi::Number>();
    }
  }
};
static FocusAreaConfig focusAreaConfig;

Napi::Object
nativeVideoRecorder(const Napi::CallbackInfo &info)
{

  Napi::Env env = info.Env();
  Napi::Object ret = Napi::Object::New(env);
  std::string op;
  VideoController::StatusInfo lastStatusInfo;
  ret.Set("status", Napi::String::New(env, "OK"));
  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of argumentps")
        .ThrowAsJavaScriptException();
    return ret;
  }

  auto args = info[0].As<Napi::Object>();
  if (!args.Has("op"))
  {
    Napi::TypeError::New(env, "Missing op field").ThrowAsJavaScriptException();
    return ret;
  }

  try
  {
    op = args.Get("op").As<Napi::String>().Utf8Value();
    if (!videoController)
    {
      videoController = std::shared_ptr<VideoController>(new VideoController());
    }
    if (!viscaClient)
    {
      viscaClient = createViscaTcpClient(
          viscaStatusLogger,
          viscaStateLogger,
          5, // connect timeout
          2  // send timeout
      );
    }
    if (op == "settings")
    {
      if (!args.Has("props"))
      {
        Napi::TypeError::New(env, "Missing props field")
            .ThrowAsJavaScriptException();
        return ret;
      }
      auto props = args.Get("props").As<Napi::Object>();
      if (props.Has("waypoint"))
      {
        auto waypoint = props.Get("waypoint").As<Napi::String>().Utf8Value();
        videoController->setWaypoint(waypoint);
      }
      else if (props.Has("focusArea"))
      {
        auto focusArea = props.Get("focusArea").As<Napi::Object>();
        focusAreaConfig.setFromNapi(focusArea);
      }
      return ret;
    }
    else if (op == "start-recording")
    {
      if (!args.Has("props"))
      {
        Napi::TypeError::New(env, "Missing props field")
            .ThrowAsJavaScriptException();
        return ret;
      }

      auto props = args.Get("props").As<Napi::Object>();
      std::vector<std::string> prop_names = {
          "recordingFolder", "recordingPrefix", "recordingDuration",
          "networkCamera", "cropArea", "guide"};
      for (const auto &name : prop_names)
      {
        if (!props.Has(name.c_str()))
        {
          std::stringstream ss;
          ss << "Missing recordingProp: " << name;
          Napi::TypeError::New(env, ss.str()).ThrowAsJavaScriptException();
          return ret;
        }
      }
      bool reportAllGaps = false;
      if (props.Has("reportAllGaps"))
      {
        reportAllGaps = props.Get("reportAllGaps").As<Napi::Boolean>();
      }
      auto protocol = getNapiStringField(props, "protocol", "SRT");
      auto folder = props.Get("recordingFolder").As<Napi::String>().Utf8Value();
      auto prefix = getNapiStringField(props, "recordingPrefix", "CT_");
      auto networkCamera =
          props.Get("networkCamera").As<Napi::String>().Utf8Value();
      auto interval =
          props.Get("recordingDuration").As<Napi::Number>().Uint32Value();
      auto cropArea = props.Get("cropArea").As<Napi::Object>();

      if (cropArea.Has("x") && cropArea.Has("y") && cropArea.Has("width") &&
          cropArea.Has("height"))
      {
        cropRect = FrameProcessor::FRectangle{
            cropArea.Get("x").As<Napi::Number>().FloatValue(),
            cropArea.Get("y").As<Napi::Number>().FloatValue(),
            cropArea.Get("width").As<Napi::Number>().FloatValue(),
            cropArea.Get("height").As<Napi::Number>().FloatValue()};
      }
      auto guideObj = props.Get("guide").As<Napi::Object>();
      guide.pt1 = guideObj.Get("pt1").As<Napi::Number>().FloatValue();
      guide.pt2 = guideObj.Get("pt2").As<Napi::Number>().FloatValue();

      auto result = videoController->start(networkCamera, protocol, "ffmpeg", folder, prefix,
                                           interval, cropRect, guide, reportAllGaps);
      if (!result.empty())
      {
        std::cerr << "Error: " << result << std::endl;
        ret.Set("status", Napi::String::New(env, "Fail"));
        ret.Set("error", Napi::String::New(env, result));
      }
      else
      {
        std::cout << "recording started" << std::endl;
      }

      return ret;
    }
    else if (op == "stop-recording")
    {
      if (videoController)
      {
        auto err = videoController->stop();
        std::cerr << "Recorder stopped with status: " << err << std::endl;
      }
      return ret;
    }
    else if (op == "get-camera-list")
    {

      if (videoController)
      {
        auto cameras = videoController->getCameraList();

        Napi::Array arr = Napi::Array::New(env, cameras.size());
        size_t index = 0;
        for (auto &camera : cameras)
        {
          auto item = Napi::Object::New(env);
          item.Set("name", Napi::String::New(env, camera.name));
          item.Set("address", Napi::String::New(env, camera.address));
          arr.Set(index++, item);
        }

        ret.Set("cameras", arr);
        ret.Set("status", Napi::String::New(env, "OK"));
      }
      else
      {
        ret.Set("status", Napi::String::New(env, "Fail"));
        ret.Set("error", Napi::String::New(env, "No recorder running"));
      }
      return ret;
    }
    else if (op == "recording-status")
    {
      if (videoController)
      {
        auto status = videoController->getStatus();
        lastStatusInfo = status;
        ret.Set("status", Napi::String::New(env, "OK"));
        ret.Set("error", Napi::String::New(env, status.error));
        ret.Set("recording", Napi::Boolean::New(env, status.recording));
        if (status.recording)
        {

          ret.Set("recordingDuration",
                  Napi::Number::New(env, status.recordingDuration));
          auto frameProcessor = Napi::Object::New(env);
          ret.Set("frameProcessor", frameProcessor);
          frameProcessor.Set(
              "recording",
              Napi::Boolean::New(env, status.frameProcessor.recording));
          frameProcessor.Set(
              "error", Napi::String::New(env, status.frameProcessor.error));
          frameProcessor.Set(
              "filename",
              Napi::String::New(env, status.frameProcessor.filename));
          frameProcessor.Set(
              "width", Napi::Number::New(env, status.frameProcessor.width));
          frameProcessor.Set(
              "height", Napi::Number::New(env, status.frameProcessor.height));
          frameProcessor.Set("fps",
                             Napi::Number::New(env, status.frameProcessor.fps));
          frameProcessor.Set("frameBacklog", Napi::Number::New(env, status.frameProcessor.frameBacklog));
          frameProcessor.Set("lastTsMilli", Napi::Number::New(env, status.frameProcessor.lastTsMilli));
        }
      }
      else
      {
        ret.Set("status", Napi::String::New(env, "OK"));
      }
      return ret;
    }
    else if (op == "grab-frame")
    {
      // grab a JPEG-encoded frame from the input stream
      if (!videoController)
      {
        return ret;
      }

      auto videoFrame = videoController->getLastFrame();
      if (!videoFrame)
      {
        return ret;
      }

      auto jpegBuffer = encodeFrameAsJpeg(videoFrame, 75);
      if (jpegBuffer.empty())
        return ret;

      auto bufferData = Napi::Buffer<uint8_t>::New(env, jpegBuffer.size());
      std::copy(jpegBuffer.begin(), jpegBuffer.end(), bufferData.Data());

      double focusScore = 0.0;
      if (focusAreaConfig.enabled)
      {
        auto x = static_cast<int>(focusAreaConfig.xPct * videoFrame->xres) & ~1;
        auto y = static_cast<int>(focusAreaConfig.yPct * videoFrame->yres);
        cv::Point center(x, y);
        focus::Options opt;
        opt.roiSize = static_cast<int>(videoFrame->yres * focusAreaConfig.sizePct) & ~1;
        if (opt.roiSize < 32)
        {
          opt.roiSize = std::max(64, videoFrame->yres / 8);
          std::cerr << "pct: " << focusAreaConfig.sizePct << " roi=" << opt.roiSize << std::endl;
        }

        int roi_x = std::max(0, x - opt.roiSize / 2) & ~1;
        int roi_y = std::max(0, y - opt.roiSize / 2);
        int roi_w = std::min(opt.roiSize, videoFrame->xres - roi_x) & ~1;
        int roi_h = std::min(opt.roiSize, videoFrame->yres - roi_y);
        cv::Rect roiRect(roi_x, roi_y, roi_w, roi_h);

        cv::Mat gray;
        if (videoFrame->pixelFormat == Frame::PixelFormat::YUV420P)
        {
          // Y plane is already luma — extract ROI directly, no conversion needed
          cv::Mat yPlane(videoFrame->yres, videoFrame->xres, CV_8UC1,
                         videoFrame->data, videoFrame->xres);
          gray = yPlane(roiRect).clone();
        }
        else
        {
          cv::Mat fullUyvy(videoFrame->yres, videoFrame->xres, CV_8UC2,
                           videoFrame->data, videoFrame->stride);
          cv::cvtColor(fullUyvy(roiRect), gray, cv::COLOR_YUV2GRAY_UYVY);
        }

        focusScore = focus::scoreAt(gray, center, opt);
      }

      ret.Set("data", bufferData);
      ret.Set("width", Napi::Number::New(env, videoFrame->xres));
      ret.Set("height", Napi::Number::New(env, videoFrame->yres));
      ret.Set("totalBytes", Napi::Number::New(env, jpegBuffer.size()));
      ret.Set("tsMilli", Napi::Number::New(env, videoFrame->timestamp / 10000));
      ret.Set("focus", Napi::Number::New(env, focusScore));
      return ret;
    }
    else if (op == "send-visca-cmd")
    {
      if (!args.Has("props"))
      {
        Napi::TypeError::New(env, "Missing props field")
            .ThrowAsJavaScriptException();
        return ret;
      }

      auto props = args.Get("props").As<Napi::Object>();
      if (!props.Has("data"))
      {
        Napi::TypeError::New(env, "Missing data field for send-visca-cmd")
            .ThrowAsJavaScriptException();
        return ret;
      }
      if (!props.Has("id"))
      {
        Napi::TypeError::New(env, "Missing ip field for send-visca-cmd")
            .ThrowAsJavaScriptException();
        return ret;
      }
      if (!props.Has("ip"))
      {
        Napi::TypeError::New(env, "Missing ip field for send-visca-cmd")
            .ThrowAsJavaScriptException();
        return ret;
      }
      uint16_t port = 52381;
      if (props.Has("port"))
      {
        port = props.Get("port").As<Napi::Number>().Uint32Value();
      }
      auto id = props.Get("id").As<Napi::String>().Utf8Value();
      auto ip = props.Get("ip").As<Napi::String>().Utf8Value();
      auto payloadVal = props.Get("data");

      // Check if 'payload' exists and is a typed array
      if (payloadVal.IsUndefined() || !payloadVal.IsTypedArray())
      {
        Napi::TypeError::New(env, "'data' field must be a Uint8Array").ThrowAsJavaScriptException();
        return ret;
      }

      Napi::Buffer<uint8_t> buffer = payloadVal.As<Napi::Buffer<uint8_t>>();
      uint8_t *dataPtr = buffer.Data();
      size_t length = buffer.ElementLength();

      // Copy the data into a std::vector<uint8_t>
      std::vector<uint8_t> nativeVector(length);
      std::memcpy(nativeVector.data(), dataPtr, length);

      viscaClient->start(ip, port);
      viscaClient->sendCommand(nativeVector, [id](const ViscaResult &focusResult)
                               {
                                //  if (focusResult.status == ViscaResult::Status::OK)
                                //  {
                                //    std::cout << "Camera Command SUCCESS. Received "
                                //              << focusResult.response.size() << " bytes.\n  Hex: ";
                                //    for (auto b : focusResult.response)
                                //    {
                                //      std::cout << "0x" << std::hex << (int)b << " ";
                                //    }
                                //    std::cout << std::dec << std::endl;
                                //  }
                                 json result = {{"id", id}, {"status", focusResult.status}};
                                 if (focusResult.response.size() > 0)
                                 {
                                   result["data"] = focusResult.response;
                                 }

                                 sendMessageToRenderer("visca-result", std::make_shared<json>(result)); });

      ret.Set("status", Napi::String::New(env, "OK"));
      return ret;
    }

    std::cerr << "Unrecognized op: " << op << std::endl;
    Napi::TypeError::New(env, "Unrecognized op field")
        .ThrowAsJavaScriptException();
  }
  catch (const std::exception &e)
  {
    std::cerr << "Error while processing op=" << op << std::endl;
    std::cerr << "Last statusInfo=" << lastStatusInfo << std::endl;
    // Catch standard C++ exceptions and convert them to JavaScript errors
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (...)
  {
    // Catch all other types of exceptions and throw a generic JavaScript
    // error
    std::cerr << "Error while processing op=" << op << std::endl;
    std::cerr << "Last statusInfo=" << lastStatusInfo << std::endl;
    Napi::Error::New(env, "An unknown error occurred")
        .ThrowAsJavaScriptException();
  }

  return ret;
}

std::ofstream logFile;

Napi::Value shutdownRecorder(const Napi::CallbackInfo &info)
{
  deInitThreadSafeFunction();
  Napi::Env env = info.Env();
  videoController = nullptr;
  std::cerr << "Recorder shutdown" << std::endl;
  if (viscaClient)
  {
    std::cerr << "Requesting VISCA client to stop" << std::endl;
    viscaClient->stop();
    viscaClient = nullptr;
  }
  std::cerr << "VISCA client stopped" << std::endl;
  logFile.close();
  return env.Undefined();
}

Napi::Value setLogFile(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  std::string logFilename = info[0].As<Napi::String>();
  logFile = std::ofstream(logFilename.c_str(), std::ios::app);
  // Redirect cout and cerr to the log file
  std::cout.rdbuf(logFile.rdbuf());
  std::cerr.rdbuf(logFile.rdbuf());
  return env.Undefined();
}

// Initialize the addon
Napi::Object Init(Napi::Env env, Napi::Object exports)
{
#ifdef __APPLE__
  triggerMacOSLocalNetworkPermission();
#endif
  exports.Set(Napi::String::New(env, "nativeVideoRecorder"),
              Napi::Function::New(env, nativeVideoRecorder));
  exports.Set("setNativeMessageCallback",
              Napi::Function::New(env, initThreadSafeFunction));
  exports.Set("setLogFile", Napi::Function::New(env, setLogFile));
  exports.Set("shutdownRecorder", Napi::Function::New(env, shutdownRecorder));
  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
