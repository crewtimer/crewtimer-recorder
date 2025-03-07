#include <memory>
#include <string>

#include "nlohmann/json.hpp"

void sendMessageToRenderer(const std::string &sender,
                           std::shared_ptr<nlohmann::json> content);
