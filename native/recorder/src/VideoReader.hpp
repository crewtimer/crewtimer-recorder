#pragma once

#include "FrameProcessor.hpp"
#include <memory>
#include <string>

class VideoReader {
public:
  typedef struct CameraInfo {
    std::string name;
    std::string address;
    CameraInfo() {}
    CameraInfo(std::string name, std::string address)
        : name(name), address(address) {}
  } CameraInfo;

  virtual std::string start(const std::string srcName,
                            std::shared_ptr<FrameProcessor> frameProcessor) = 0;
  virtual std::string stop() = 0;
  virtual std::vector<CameraInfo> getCameraList() {
    return std::vector<CameraInfo>();
  };
  virtual ~VideoReader() {}
};

std::shared_ptr<VideoReader> createBaslerReader();
std::shared_ptr<VideoReader> createNdiReader();