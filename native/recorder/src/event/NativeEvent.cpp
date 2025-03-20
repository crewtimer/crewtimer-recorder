#include <napi.h>
#include <node.h>
#include <uv.h>
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
      // For now, assume uint8_t
      std::vector<uint8_t> dataVec = it.value().get<std::vector<uint8_t>>();
      // std::cerr << "Encoding Array of len " << dataVec.size() << std::endl;
      Napi::Array arr = Napi::Array::New(env, dataVec.size());
      size_t index = 0;
      for (auto &el : dataVec)
      {
        arr.Set(index++, el);
      }
      obj.Set(it.key(), arr);
      // std::cerr << "Encoding array done" << std::endl;
    }
  }
  return obj;
}

Napi::ThreadSafeFunction tsfn;

// Function to send message to Electron main process
void sendMessageToRenderer(const std::string &sender,
                           std::shared_ptr<json> content)
{
  if (!tsfn)
  {
    return;
  }
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
Napi::Value initThreadSafeFunction(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  Napi::Function callback = info[0].As<Napi::Function>();
  tsfn = Napi::ThreadSafeFunction::New(env, callback, "NativeEmitter", 0, 1);
  return env.Undefined();
}

void deInitThreadSafeFunction()
{
  if (tsfn)
  {
    tsfn.Abort(); // or tsfn.Release();
  }

  tsfn = Napi::ThreadSafeFunction(); // return to uninitialized state
}
