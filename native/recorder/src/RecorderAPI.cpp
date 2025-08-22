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

using json = nlohmann::json;
std::shared_ptr<VideoController> videoController;
std::unique_ptr<IViscaTcpClient> viscaClient;

// Utility function to clamp a value between 0 and 255
inline uint8_t clamp(int value)
{
  return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

// Function to convert a UYVY422 buffer to a preallocated RGBA buffer
void uyvyToRgba(const uint8_t *uyvyBuffer, uint8_t *rgbaBuffer, int width,
                int height, int stride)
{
  // Initialize index for the RGBA buffer
  int rgbaIndex = 0;
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; x += 2)
    {
      // Each UYVY pixel pair contains four bytes
      int uyvyIndex = y * stride + x * 2;
      uint8_t u = uyvyBuffer[uyvyIndex];
      uint8_t y0 = uyvyBuffer[uyvyIndex + 1];
      uint8_t v = uyvyBuffer[uyvyIndex + 2];
      uint8_t y1 = uyvyBuffer[uyvyIndex + 3];

      // Convert YUV to RGB for each pixel
      auto convertYuvToRgb = [](uint8_t y, uint8_t u, uint8_t v)
      {
        int c = y - 16;
        int d = u - 128;
        int e = v - 128;

        int r = clamp((298 * c + 409 * e + 128) >> 8);
        int g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        int b = clamp((298 * c + 516 * d + 128) >> 8);

        return std::tuple<uint8_t, uint8_t, uint8_t>(r, g, b);
      };

      // First pixel (y0)
      auto pixels = convertYuvToRgb(y0, u, v);
      auto r0 = std::get<0>(pixels);
      auto g0 = std::get<1>(pixels);
      auto b0 = std::get<2>(pixels);

      rgbaBuffer[rgbaIndex] = r0;
      rgbaBuffer[rgbaIndex + 1] = g0;
      rgbaBuffer[rgbaIndex + 2] = b0;
      rgbaBuffer[rgbaIndex + 3] = 255; // Alpha channel
      rgbaIndex += 4;

      // Second pixel (y1)
      pixels = convertYuvToRgb(y1, u, v);
      auto r1 = std::get<0>(pixels);
      auto g1 = std::get<1>(pixels);
      auto b1 = std::get<2>(pixels);

      rgbaBuffer[rgbaIndex] = r1;
      rgbaBuffer[rgbaIndex + 1] = g1;
      rgbaBuffer[rgbaIndex + 2] = b1;
      rgbaBuffer[rgbaIndex + 3] = 255; // Alpha channel
      rgbaIndex += 4;
    }
  }
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

Napi::Object
nativeVideoRecorder(const Napi::CallbackInfo &info)
{

  Napi::Env env = info.Env();
  Napi::Object ret = Napi::Object::New(env);
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
    auto op = args.Get("op").As<Napi::String>().Utf8Value();
    if (!videoController)
    {
      videoController = std::shared_ptr<VideoController>(new VideoController("ndi"));
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
      bool addTimeOverlay = false;
      if (props.Has("addTimeOverlay"))
      {
        addTimeOverlay = props.Get("addTimeOverlay").As<Napi::Boolean>();
      }
      auto folder = props.Get("recordingFolder").As<Napi::String>().Utf8Value();
      auto prefix = props.Get("recordingPrefix").As<Napi::String>().Utf8Value();
      auto networkCamera =
          props.Get("networkCamera").As<Napi::String>().Utf8Value();
      auto interval =
          props.Get("recordingDuration").As<Napi::Number>().Uint32Value();
      auto cropArea = props.Get("cropArea").As<Napi::Object>();

      auto cropRect = FrameProcessor::FRectangle{0, 0, 0, 0};
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
      FrameProcessor::Guide guide;
      guide.pt1 = guideObj.Get("pt1").As<Napi::Number>().FloatValue();
      guide.pt2 = guideObj.Get("pt2").As<Napi::Number>().FloatValue();

      auto result = videoController->start(networkCamera, "ffmpeg", folder, prefix,
                                           interval, cropRect, guide, reportAllGaps, addTimeOverlay);
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
      // grab a rgba frame from the input stream
      if (!videoController)
      {
        return ret;
      }

      auto uyvy422Frame = videoController->getLastFrame();
      if (!uyvy422Frame)
      {
        return ret;
      }

      size_t totalBytes = 4 * uyvy422Frame->xres * uyvy422Frame->yres;
      // uint8_t *bufferData = new uint8_t[totalBytes];

      auto bufferData = Napi::Buffer<uint8_t>::New(env, totalBytes);
      uyvyToRgba(uyvy422Frame->data, bufferData.Data(), uyvy422Frame->xres,
                 uyvy422Frame->yres, uyvy422Frame->stride);
      // Napi::Buffer<uint8_t> napiBuffer = Napi::Buffer<uint8_t>::New(
      //     env, bufferData, totalBytes, FinalizeBuffer);
      // ret.Set("data", napiBuffer);
      ret.Set("data", bufferData);
      ret.Set("width", Napi::Number::New(env, uyvy422Frame->xres));
      ret.Set("height", Napi::Number::New(env, uyvy422Frame->yres));
      ret.Set("totalBytes", Napi::Number::New(env, totalBytes));
      ret.Set("tsMilli", Napi::Number::New(env, uyvy422Frame->timestamp / 10000));
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
                                 if (focusResult.status == ViscaResult::Status::OK)
                                 {
                                   std::cout << "Camera Command SUCCESS. Received "
                                             << focusResult.response.size() << " bytes.\n  Hex: ";
                                   for (auto b : focusResult.response)
                                   {
                                     std::cout << "0x" << std::hex << (int)b << " ";
                                   }
                                   std::cout << std::dec << std::endl;
                                 }
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
    // Catch standard C++ exceptions and convert them to JavaScript errors
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
  catch (...)
  {
    // Catch all other types of exceptions and throw a generic JavaScript
    // error
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
  exports.Set(Napi::String::New(env, "nativeVideoRecorder"),
              Napi::Function::New(env, nativeVideoRecorder));
  exports.Set("setNativeMessageCallback",
              Napi::Function::New(env, initThreadSafeFunction));

  exports.Set("setLogFile", Napi::Function::New(env, setLogFile));
  exports.Set("shutdownRecorder", Napi::Function::New(env, shutdownRecorder));

  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
