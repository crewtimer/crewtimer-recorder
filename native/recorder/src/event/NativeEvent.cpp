#include <napi.h>
#include <node.h>
#include <mutex>
#include <iostream>
#include "NativeEvent.hpp"

using json = nlohmann::json;

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
      const auto &arrJson = it.value();
      if (arrJson.empty())
      {
        obj.Set(it.key(), Napi::Array::New(env, 0));
      }
      else if (arrJson.at(0).is_string())
      {
        Napi::Array arr = Napi::Array::New(env, arrJson.size());
        size_t index = 0;
        for (const auto &el : arrJson)
        {
          arr.Set(index++, Napi::String::New(env, el.get<std::string>()));
        }
        obj.Set(it.key(), arr);
      }
      else if (arrJson.at(0).is_number_integer())
      {
        std::vector<uint8_t> dataVec = arrJson.get<std::vector<uint8_t>>();
        Napi::Array arr = Napi::Array::New(env, dataVec.size());
        size_t index = 0;
        for (auto &el : dataVec)
        {
          arr.Set(index++, el);
        }
        obj.Set(it.key(), arr);
      }
      else
      {
        // General fallback: try to convert elements generically (e.g. recursive call)
        Napi::Array arr = Napi::Array::New(env, arrJson.size());
        size_t index = 0;
        for (const auto &el : arrJson)
        {
          if (el.is_string())
          {
            arr.Set(index, Napi::String::New(env, el.get<std::string>()));
          }
          else if (el.is_number())
          {
            arr.Set(index, Napi::Number::New(env, el));
          }
          else if (el.is_boolean())
          {
            arr.Set(index, Napi::Boolean::New(env, el));
          }
          else if (el.is_object())
          {
            arr.Set(index, ConvertJsonToNapiObject(env, el));
          }
          else
          {
            arr.Set(index, env.Null());
          }
          ++index;
        }
        obj.Set(it.key(), arr);
      }
    }
  }
  return obj;
}

Napi::ThreadSafeFunction tsfn;
// Guards tsfn itself: messages are sent from many threads while the main thread
// may be initializing or tearing it down.
std::mutex tsfnMutex;

// Sends a message to the Electron main process. Safe to call from any thread: the
// ThreadSafeFunction queues the call onto the JS thread. (libuv handles must not be
// created off the loop thread, which an earlier uv_async-based version did.)
void sendMessageToRenderer(const std::string &sender,
                           std::shared_ptr<json> content)
{
  std::lock_guard<std::mutex> lock(tsfnMutex);
  if (!tsfn)
  {
    return;
  }
  // The queue is unbounded, so this never blocks; it fails only while closing.
  tsfn.NonBlockingCall(
      [sender, content](Napi::Env env, Napi::Function jsCallback)
      {
        Napi::Object message = Napi::Object::New(env);
        message.Set("sender", Napi::String::New(env, sender));
        message.Set("content", ConvertJsonToNapiObject(env, *content));
        jsCallback.Call({message});
      });
}

// Initialize the ThreadSafeFunction
Napi::Value initThreadSafeFunction(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::Function callback = info[0].As<Napi::Function>();
  std::lock_guard<std::mutex> lock(tsfnMutex);
  tsfn = Napi::ThreadSafeFunction::New(env, callback, "NativeEmitter", 0, 1);
  return env.Undefined();
}

void deInitThreadSafeFunction()
{
  std::lock_guard<std::mutex> lock(tsfnMutex);
  if (tsfn)
  {
    tsfn.Abort(); // or tsfn.Release();
  }

  tsfn = Napi::ThreadSafeFunction(); // return to uninitialized state
}
