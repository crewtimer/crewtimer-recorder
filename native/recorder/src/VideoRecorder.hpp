#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

/**
 * Class representing a single video frame.
 */
class Frame {
public:
  enum PixelFormat { UYVY422 = 0, RGBX = 1, BGR = 2 };
  int xres;
  int yres;
  uint8_t *data;
  uint64_t timestamp;
  int frame_rate_N;
  int frame_rate_D;
  PixelFormat pixelFormat;
  virtual ~Frame() {}
};

typedef std::shared_ptr<Frame> FramePtr;

class VideoRecorder {
public:
  virtual std::string openVideoStream(std::string directory,
                                      std::string filename, int width,
                                      int height, float fps) = 0;
  virtual std::string writeVideoFrame(FramePtr frame) = 0;
  virtual std::string stop() = 0;
  virtual ~VideoRecorder() {}
};

std::shared_ptr<VideoRecorder> createOpenCVRecorder();
std::shared_ptr<VideoRecorder> createAppleRecorder();
std::shared_ptr<VideoRecorder> createFfmpegRecorder();
std::shared_ptr<VideoRecorder> createNullRecorder();
void testopencv();
