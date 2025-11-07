#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "VideoUtils.hpp"

class VideoRecorder
{
public:
  virtual std::string openVideoStream(std::string directory,
                                      std::string filename, int width,
                                      int height, float fps, uint64_t timestamp) = 0;
  virtual std::string writeVideoFrame(FramePtr frame) = 0;
  virtual std::string stop() = 0;
  virtual int getKeyFrameInterval()
  {
    return 12;
  }
  virtual ~VideoRecorder() {}
};

std::shared_ptr<VideoRecorder> createOpenCVRecorder();
std::shared_ptr<VideoRecorder> createAppleRecorder();
std::shared_ptr<VideoRecorder> createFfmpegRecorder();
std::shared_ptr<VideoRecorder> createNullRecorder();
void testopencv();
