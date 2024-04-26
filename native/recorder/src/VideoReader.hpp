#pragma once

#include "FrameProcessor.hpp"
#include <memory>

class VideoReader {
public:
  virtual int open(std::shared_ptr<FrameProcessor> frameProcessor) = 0;
  virtual int start() = 0;
  virtual int stop() = 0;
  virtual ~VideoReader() {}
};

std::shared_ptr<VideoReader> createBaslerReader();
std::shared_ptr<VideoReader> createNdiReader(const std::string srcName);