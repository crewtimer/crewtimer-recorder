#pragma once
#include <string>               // For std::string
#include <memory>               // For std::shared_ptr
#include "../nlohmann/json.hpp" // For nlohmann::json
#include <napi.h>

void sendMessageToRenderer(const std::string &sender, std::shared_ptr<nlohmann::json> content);
Napi::Value initThreadSafeFunction(const Napi::CallbackInfo &info);
void deInitThreadSafeFunction();
