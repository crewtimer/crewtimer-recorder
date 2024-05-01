#pragma once

#include "FrameProcessor.hpp"
#include <memory>
#include <string>

class VideoReader {
public:
  virtual std::string open(std::shared_ptr<FrameProcessor> frameProcessor) = 0;
  virtual std::string start() = 0;
  virtual std::string stop() = 0;
  virtual ~VideoReader() {}
};

std::shared_ptr<VideoReader> createBaslerReader();
std::shared_ptr<VideoReader> createNdiReader(const std::string srcName);