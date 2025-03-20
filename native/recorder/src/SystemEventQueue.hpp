#pragma once
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <string>

#include "EventQueue.hpp"
#ifndef STANDALONE
#include "./event/NativeEvent.hpp"
#endif

struct SystemEvent
{
  const int64_t tsMilli;
  const std::string subsystem;
  const std::string message;
  SystemEvent(int64_t tsMilli, std::string subsystem, std::string message)
      : tsMilli(tsMilli), subsystem(subsystem), message(message) {}
};

class SystemEventQueue : public EventQueue<std::shared_ptr<SystemEvent>>
{
public:
  SystemEventQueue() : EventQueue(200) {}
  static SystemEventQueue &instance()
  {
    static SystemEventQueue singleton;
    return singleton;
  }
  static void push(const std::string &subsystem, const std::string &message)
  {

    std::cout << "Event: " << subsystem << ": " << message << std::endl;

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    std::lock_guard<std::recursive_mutex> lock(
        SystemEventQueue::instance().mutex_);
    auto queue = SystemEventQueue::instance().eventQueue;
    const auto &event = SystemEvent{now, subsystem, message};
    SystemEventQueue::instance().addEvent(
        std::shared_ptr<SystemEvent>(new SystemEvent(now, subsystem, message)));
#ifndef STANDALONE
    nlohmann::json msg = {{"tsMilli", now}, {"subsystem", subsystem}, {"message", message}};
    sendMessageToRenderer("sysevent", std::make_shared<nlohmann::json>(msg));
#endif
  }
  static std::vector<std::shared_ptr<SystemEvent>> getEventList()
  {
    std::lock_guard<std::recursive_mutex> lock(
        SystemEventQueue::instance().mutex_);
    return std::vector<std::shared_ptr<SystemEvent>>(
        SystemEventQueue::instance().eventQueue.begin(),
        SystemEventQueue::instance().eventQueue.end());
  }
};
