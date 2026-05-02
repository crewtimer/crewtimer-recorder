#pragma once
#include <stdint.h>
#include <memory>

/**
 * Class representing a single video frame.
 */
class Frame
{
public:
  enum FrameType
  {
    VIDEO = 0,
    SOURCE_DISCONNECTED = 1
  };
  enum PixelFormat
  {
    UYVY422 = 0,
    RGBX = 1,
    BGR = 2,
    YUV420P = 3 // I420: planar YUV, stride = width (Y plane); U/V planes at stride/2
  };
  int xres;
  int yres;
  int stride;
  uint8_t *data;
  uint64_t timestamp;
  int frame_rate_N;
  int frame_rate_D;
  PixelFormat pixelFormat;
  bool ownData;
  FrameType frameType = VIDEO;

  Frame() { ownData = false; }
  Frame(int width, int height, PixelFormat format)
      : xres(width), yres(height), pixelFormat(format)
  {
    if (format == YUV420P)
    {
      stride = width; // Y plane stride; U/V planes use stride/2
      data = new uint8_t[width * height * 3 / 2]; // I420 size
    }
    else
    {
      int bytesPerPixel = (format == UYVY422) ? 2 : (format == RGBX ? 4 : 3);
      stride = width * bytesPerPixel;
      data = new uint8_t[stride * height];
    }
    ownData = true;
  }

  virtual ~Frame()
  {
    if (data && ownData)
    {
      delete[] data;
      data = nullptr;
    }
  }
};

typedef std::shared_ptr<Frame> FramePtr;

FramePtr cropFrame(const FramePtr &frame, int cropX, int cropY, int cropWidth,
                   int cropHeight);
void encodeTimestamp(uint8_t *screen, int stride, uint64_t ts100ns);
void overlayTime(uint8_t *ptr, int stride, uint64_t ts100ns, const std::tm *local_time = nullptr);
