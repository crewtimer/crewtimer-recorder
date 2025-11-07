#pragma once
#include <stdint.h>
#include <memory>
#include <vector>

/**
 * Class representing a single video frame.
 */
class Frame
{
public:
  enum FrameType
  {
    VIDEO = 0,
    SOURCE_DISCONNECTED = 1,
    ENCODED_VIDEO = 2
  };
  enum PixelFormat
  {
    UYVY422 = 0,
    RGBX = 1,
    BGR = 2
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

  struct EncodedPacket
  {
    std::vector<uint8_t> data;
    std::vector<uint8_t> extradata;
    int width = 0;
    int height = 0;
    int codecId = 0;
    int timeBaseNum = 0;
    int timeBaseDen = 1;
    int avgFrameRateNum = 0;
    int avgFrameRateDen = 1;
    int64_t pts = -1;
    int64_t dts = -1;
    int64_t duration = 0;
    bool keyFrame = false;
    bool annexb = true;
  };

  EncodedPacket encodedPacket;

  bool hasEncodedData() const
  {
    return frameType == ENCODED_VIDEO && !encodedPacket.data.empty();
  }

  Frame() { ownData = false; }
  Frame(int width, int height, PixelFormat format)
      : xres(width), yres(height), pixelFormat(format)
  {
    // Calculate stride based on pixel format
    int bytesPerPixel = (format == UYVY422) ? 2 : (format == RGBX ? 4 : 3);
    stride = width * bytesPerPixel;
    data = new uint8_t[stride * height];
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
