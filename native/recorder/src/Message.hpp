#include <memory>
#include <nlohmann/json.hpp>
#include <string>

void SendMessageToElectron(const std::string &sender,
                           std::shared_ptr<nlohmann::json> content);
