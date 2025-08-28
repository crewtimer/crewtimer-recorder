#pragma once

#include <functional>
#include <memory>
#include <string>
#include "VideoUtils.hpp"

class VideoReader
{
protected:
  bool reportAllGaps = false;

public:
  using AddFrameFunction = std::function<void(FramePtr)>;
  typedef struct CameraInfo
  {
    std::string name;
    std::string address;
    uint16_t port;
    std::string url;
    CameraInfo() {}
    CameraInfo(std::string name, std::string address, uint16_t port)
        : name(name), address(address), port(port)
    {
      if (port == 0)
      {
        url = address;
      }
      else
      {
        url = address + ":" + std::to_string(port);
      }
    }
  } CameraInfo;

  virtual std::string start(const std::string srcName,
                            AddFrameFunction addFrameFunction) = 0;
  virtual std::string stop() = 0;
  virtual void setProperties(bool reportAllGaps)
  {
    this->reportAllGaps = reportAllGaps;
  }
  virtual std::vector<CameraInfo> getCameraList()
  {
    return std::vector<CameraInfo>();
  };
  virtual ~VideoReader() {}
};

std::shared_ptr<VideoReader> createBaslerReader();
std::shared_ptr<VideoReader> createNdiReader();
