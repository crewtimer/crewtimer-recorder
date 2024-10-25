#pragma once
#include <stdint.h>

/**
 * Class representing a single video frame.
 */
class Frame {
public:
  enum PixelFormat { UYVY422 = 0, RGBX = 1, BGR = 2 };
  int xres;
  int yres;
  int stride;
  uint8_t *data;
  uint64_t timestamp;
  int frame_rate_N;
  int frame_rate_D;
  PixelFormat pixelFormat;
  bool ownData;
  Frame() { ownData = false; }
  Frame(int width, int height, PixelFormat format)
      : xres(width), yres(height), pixelFormat(format) {
    // Calculate stride based on pixel format
    int bytesPerPixel = (format == UYVY422) ? 2 : (format == RGBX ? 4 : 3);
    stride = width * bytesPerPixel;
    data = new uint8_t[stride * height];
    ownData = true;
  }

  virtual ~Frame() {
    if (data && ownData) {
      delete[] data;
      data = nullptr;
    }
  }
};

typedef std::shared_ptr<Frame> FramePtr;

FramePtr cropFrame(const FramePtr &frame, int cropX, int cropY, int cropWidth,
                   int cropHeight);
void encodeTimestamp(uint8_t *screen, int stride, uint64_t ts100ns);