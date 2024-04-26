#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "util.hpp"
std::string getCurrentTimeAsString() {
  // Get the current time in system clock
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

  // Convert the current time to a time_t value (seconds since epoch)
  std::time_t time_now = std::chrono::system_clock::to_time_t(now);

  // Convert the time_t value to a struct tm (broken-down time)
  std::tm local_time = *std::localtime(&time_now);

  // Create a stringstream to build the time string
  std::stringstream ss;
  ss << std::setfill('0') << std::setw(2) << local_time.tm_hour << "_"
     << std::setw(2) << local_time.tm_min << "_" << std::setw(2)
     << local_time.tm_sec;

  return ss.str();
}
