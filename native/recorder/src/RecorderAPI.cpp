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
    }

    std::cerr << "Unrecognized op: " << op << std::endl;
    Napi::TypeError::New(env, "Unrecognized op field")
        .ThrowAsJavaScriptException();
  } catch (const std::exception &e) {
    // Catch standard C++ exceptions and convert them to JavaScript errors
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  } catch (...) {
    // Catch all other types of exceptions and throw a generic JavaScript error
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
