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
#include <uv.h>

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
#include "nlohmann/json.hpp"

using json = nlohmann::json;
std::shared_ptr<VideoController> recorder;

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

// Helper function to convert nlohmann::json to Napi::Object
Napi::Object ConvertJsonToNapiObject(Napi::Env env, const json &j)
{
  Napi::Object obj = Napi::Object::New(env);
  for (auto it = j.begin(); it != j.end(); ++it)
  {
    if (it.value().is_string())
    {
      const std::string s = it.value();
      obj.Set(it.key(), Napi::String::New(env, s));
    }
    else if (it.value().is_number_integer())
    {
      obj.Set(it.key(), Napi::Number::New(env, it.value()));
    }
    else if (it.value().is_number_float())
    {
      obj.Set(it.key(), Napi::Number::New(env, it.value()));
    }
    else if (it.value().is_boolean())
    {
      obj.Set(it.key(), Napi::Boolean::New(env, it.value()));
    }
    else if (it.value().is_object())
    {
      obj.Set(it.key(), ConvertJsonToNapiObject(env, it.value()));
    }
    else if (it.value().is_array())
    {
      Napi::Array arr = Napi::Array::New(env, it.value().size());
      size_t index = 0;
      for (auto &el : it.value())
      {
        arr.Set(index++, ConvertJsonToNapiObject(env, el));
      }
      obj.Set(it.key(), arr);
    }
  }
  return obj;
}

// Define a destructor to free uint8_t buffers
void FinalizeBuffer(Napi::Env env, void *data)
{
  // Clean up memory if necessary
  delete[] static_cast<uint8_t *>(data);
}
Napi::Object nativeVideoRecorder(const Napi::CallbackInfo &info)
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
    if (!recorder)
    {
      recorder = std::shared_ptr<VideoController>(new VideoController("ndi"));
    }
    if (op == "start-recording")
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

      auto result = recorder->start(networkCamera, "ffmpeg", folder, prefix,
                                    interval, cropRect, guide);
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
      if (recorder)
      {
        auto err = recorder->stop();
        std::cerr << "Recorder stopped with status: " << err << std::endl;
      }
      return ret;
    }
    else if (op == "get-camera-list")
    {

      if (recorder)
      {
        auto cameras = recorder->getCameraList();

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
      if (recorder)
      {
        auto status = recorder->getStatus();
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
        }
      }
      else
      {
        ret.Set("status", Napi::String::New(env, "OK"));
      }
      return ret;
    }
    else if (op == "recording-log")
    {
      // TODO - perhaps avoid the copy and make friend class to access the
      // list for serialization
      auto list = SystemEventQueue::getEventList();
      ret.Set("list", convertEventsToJS(env, list));
      return ret;
    }
    else if (op == "grab-frame")
    {
      // grab a rgba frame from the input stream
      if (!recorder)
      {
        return ret;
      }

      auto uyvy422Frame = recorder->getLastFrame();
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

Napi::ThreadSafeFunction tsfn;

// Function to send message to Electron main process
void SendMessageToElectron(const std::string &sender,
                           std::shared_ptr<json> content)
{
  uv_async_t *async = new uv_async_t;
  uv_loop_t *loop = uv_default_loop();
  auto *data =
      new std::pair<std::string, std::shared_ptr<json>>(sender, content);
  async->data = data;

  uv_async_init(loop, async, [](uv_async_t *handle)
                {
    auto *data = static_cast<std::pair<std::string, std::shared_ptr<json>> *>(
        handle->data);
    std::string sender = data->first;
    std::shared_ptr<json> content = data->second;

    tsfn.BlockingCall(
        [sender, content](Napi::Env env, Napi::Function jsCallback) {
          Napi::Object message = Napi::Object::New(env);
          message.Set("sender", Napi::String::New(env, sender));
          message.Set("content", ConvertJsonToNapiObject(env, *content));
          jsCallback.Call({message});
        });
    delete data;
    uv_close(reinterpret_cast<uv_handle_t *>(handle),
             [](uv_handle_t *handle) { delete handle; }); });

  uv_async_send(async);
}

// Initialize the ThreadSafeFunction
Napi::Value InitThreadSafeFunction(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::Function callback = info[0].As<Napi::Function>();
  tsfn = Napi::ThreadSafeFunction::New(env, callback, "NativeEmitter", 0, 1);
  return env.Undefined();
}

std::ofstream logFile;

Napi::Value shutdownRecorder(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  recorder = nullptr;
  tsfn = Napi::ThreadSafeFunction();
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
              Napi::Function::New(env, InitThreadSafeFunction));

  exports.Set("setLogFile", Napi::Function::New(env, setLogFile));
  exports.Set("shutdownRecorder", Napi::Function::New(env, shutdownRecorder));

  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
