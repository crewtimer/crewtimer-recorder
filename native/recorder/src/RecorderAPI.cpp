#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <napi.h>
#include <node.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include "SystemEventQueue.hpp"
#include "VideoController.hpp"

std::shared_ptr<VideoController> recorder;

// Utility function to clamp a value between 0 and 255
inline uint8_t clamp(int value) {
  return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

// Function to convert a UYVY422 buffer to a preallocated RGBA buffer
void uyvyToRgba(const uint8_t *uyvyBuffer, uint8_t *rgbaBuffer, int width,
                int height, int stride) {
  // Initialize index for the RGBA buffer
  int rgbaIndex = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; x += 2) {
      // Each UYVY pixel pair contains four bytes
      int uyvyIndex = y * stride + x * 2;
      uint8_t u = uyvyBuffer[uyvyIndex];
      uint8_t y0 = uyvyBuffer[uyvyIndex + 1];
      uint8_t v = uyvyBuffer[uyvyIndex + 2];
      uint8_t y1 = uyvyBuffer[uyvyIndex + 3];

      // Convert YUV to RGB for each pixel
      auto convertYuvToRgb = [](uint8_t y, uint8_t u, uint8_t v) {
        int c = y - 16;
        int d = u - 128;
        int e = v - 128;

        int r = clamp((298 * c + 409 * e + 128) >> 8);
        int g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        int b = clamp((298 * c + 516 * d + 128) >> 8);

        return std::tuple<uint8_t, uint8_t, uint8_t>(r, g, b);
      };

      // First pixel (y0)
      auto [r0, g0, b0] = convertYuvToRgb(y0, u, v);
      rgbaBuffer[rgbaIndex] = r0;
      rgbaBuffer[rgbaIndex + 1] = g0;
      rgbaBuffer[rgbaIndex + 2] = b0;
      rgbaBuffer[rgbaIndex + 3] = 255; // Alpha channel
      rgbaIndex += 4;

      // Second pixel (y1)
      auto [r1, g1, b1] = convertYuvToRgb(y1, u, v);
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
                  const std::vector<std::shared_ptr<SystemEvent>> &eventList) {
  Napi::Array jsArray = Napi::Array::New(env);

  for (size_t i = 0; i < eventList.size(); ++i) {
    Napi::Object jsEvent = Napi::Object::New(env);
    jsEvent.Set("tsMilli", Napi::Number::New(env, eventList[i]->tsMilli));
    jsEvent.Set("subsystem", Napi::String::New(env, eventList[i]->subsystem));
    jsEvent.Set("message", Napi::String::New(env, eventList[i]->message));

    jsArray[i] = jsEvent;
  }

  return jsArray;
}

// Define a destructor to free uint8_t buffers
void FinalizeBuffer(Napi::Env env, void *data) {
  // Clean up memory if necessary
  delete[] static_cast<uint8_t *>(data);
}
Napi::Object nativeVideoRecorder(const Napi::CallbackInfo &info) {
  // std::cerr << "nativeVideoRecorder add-on" << std::endl;

  Napi::Env env = info.Env();
  Napi::Object ret = Napi::Object::New(env);
  ret.Set("status", Napi::String::New(env, "OK"));
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Wrong number of argumentps")
        .ThrowAsJavaScriptException();
    return ret;
  }

  auto args = info[0].As<Napi::Object>();
  if (!args.Has("op")) {
    Napi::TypeError::New(env, "Missing op field").ThrowAsJavaScriptException();
    return ret;
  }

  try {
    auto op = args.Get("op").As<Napi::String>().Utf8Value();
    if (op == "start-recording") {
      if (!args.Has("props")) {
        Napi::TypeError::New(env, "Missing props field")
            .ThrowAsJavaScriptException();
        return ret;
      }

      auto props = args.Get("props").As<Napi::Object>();
      std::vector<std::string> prop_names = {
          "recordingFolder", "recordingPrefix", "recordingInterval"};
      for (const auto &name : prop_names) {
        if (!props.Has(name.c_str())) {
          Napi::TypeError::New(env, "Missing recordingProp")
              .ThrowAsJavaScriptException();
          return ret;
        }
      }
      auto folder = props.Get("recordingFolder").As<Napi::String>().Utf8Value();
      auto prefix = props.Get("recordingPrefix").As<Napi::String>().Utf8Value();
      auto interval =
          props.Get("recordingInterval").As<Napi::Number>().Uint32Value();
      recorder = std::shared_ptr<VideoController>(
          new VideoController("", "ffmpeg", folder, prefix, interval));
      auto result = recorder->start();
      if (!result.empty()) {
        std::cerr << "Error: " << result << std::endl;
        ret.Set("status", Napi::String::New(env, "Fail"));
        ret.Set("error", Napi::String::New(env, result));
      } else {
        std::cout << "recording started" << std::endl;
      }

      return ret;
    } else if (op == "stop-recording") {
      if (recorder) {
        auto err = recorder->stop();
        std::cerr << "Recorder stoped with status: " << err << std::endl;
        recorder = nullptr;
      }
      return ret;
    } else if (op == "recording-status") {
      if (recorder) {
        auto status = recorder->getStatus();
        ret.Set("status", Napi::String::New(env, "OK"));
        ret.Set("error", Napi::String::New(env, status.error));
        ret.Set("recording", Napi::Boolean::New(env, status.recording));
        ret.Set("recordingDuration",
                Napi::Number::New(env, status.recordingDuration));
        auto frameProcessor = Napi::Object::New(env);
        ret.Set("frameProcessor", frameProcessor);
        frameProcessor.Set(
            "recording",
            Napi::Boolean::New(env, status.frameProcessor.recording));
        frameProcessor.Set("error",
                           Napi::String::New(env, status.frameProcessor.error));
        frameProcessor.Set(
            "filename", Napi::String::New(env, status.frameProcessor.filename));
        frameProcessor.Set("width",
                           Napi::Number::New(env, status.frameProcessor.width));
        frameProcessor.Set(
            "height", Napi::Number::New(env, status.frameProcessor.height));
        frameProcessor.Set("fps",
                           Napi::Number::New(env, status.frameProcessor.fps));
      } else {
        ret.Set("status", Napi::String::New(env, "OK"));
      }
      return ret;
    } else if (op == "recording-log") {
      // TODO - perhaps avoid the copy and make friend class to access the list
      // for serialization
      auto list = SystemEventQueue::getEventList();
      ret.Set("list", convertEventsToJS(env, list));
      return ret;
    } else if (op == "grab-frame") {
      // grab a rgba frame from the input stream
      if (!recorder) {
        return ret;
      }

      auto uyvy422Frame = recorder->getLastFrame();
      if (!uyvy422Frame) {
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
  } catch (const std::exception &e) {
    // Catch standard C++ exceptions and convert them to JavaScript errors
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  } catch (...) {
    // Catch all other types of exceptions and throw a generic JavaScript
    // error
    Napi::Error::New(env, "An unknown error occurred")
        .ThrowAsJavaScriptException();
  }

  return ret;
}

// Initialize the addon
Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "nativeVideoRecorder"),
              Napi::Function::New(env, nativeVideoRecorder));

  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
