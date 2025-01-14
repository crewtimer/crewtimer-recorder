#include <algorithm>
#include <iostream>
#include <stdint.h>
#include "VideoUtils.hpp"

FramePtr cropFrame(const FramePtr &frame, int cropX, int cropY, int cropWidth,
                   int cropHeight)
{
  if (!frame || cropX < 0 || cropY < 0 || cropX + cropWidth > frame->xres ||
      cropY + cropHeight > frame->yres)
  {
    std::cerr << "Invalid crop.  frame: " << !frame << " cropX < 0:" << (cropX < 0) << " cropY < 0:" << (cropY < 0) << "cropX + cropWidth > frame->xres:" << (cropX + cropWidth > frame->xres) << " cropY + cropHeight > frame->yres:" << (cropY + cropHeight > frame->yres) << " xres: " << frame->xres << " yres: " << frame->yres << std::endl;
    return nullptr; // Return null if invalid crop dimensions
  }

  auto croppedFrame =
      std::make_shared<Frame>(cropWidth, cropHeight, frame->pixelFormat);
  croppedFrame->timestamp = frame->timestamp;
  croppedFrame->frame_rate_N = frame->frame_rate_N;
  croppedFrame->frame_rate_D = frame->frame_rate_D;

  int bytesPerPixel = (frame->pixelFormat == Frame::UYVY422)
                          ? 2
                          : (frame->pixelFormat == Frame::RGBX ? 4 : 3);

  for (int y = 0; y < cropHeight; ++y)
  {
    uint8_t *srcPtr =
        frame->data + ((cropY + y) * frame->stride) + (cropX * bytesPerPixel);
    uint8_t *dstPtr = croppedFrame->data + (y * croppedFrame->stride);
    std::memcpy(dstPtr, srcPtr, cropWidth * bytesPerPixel);
  }

  return croppedFrame;
}

static uint32_t uyvy422(uint8_t r, uint8_t g, uint8_t b)
{
  // https://github.com/lplassman/V4L2-to-NDI/blob/4dd5e9594acc4f154658283ee52718fa58018ac9/PixelFormatConverter.cpp
  auto Y0 = (0.299f * r + 0.587f * g + 0.114f * b);
  auto Y1 = (0.299f * r + 0.587f * g + 0.114f * b);
  auto U = std::max(0.0f, std::min(255.0f, 0.492f * (b - Y0) + 128.0f));
  auto V = std::max(0.0f, std::min(255.0f, 0.877f * (r - Y0) + 128.0f));
  uint32_t result =
      uint8_t(Y1) << 24 | uint8_t(V) << 16 | uint8_t(Y0) << 8 | uint8_t(U);
  // printf("Y0=%d, Y1=%d, U=%d, V=%d, %f\n", uint8_t(Y0), uint8_t(Y1),
  // uint8_t(U), uint8_t(V),
  //        0.492f * (b - Y0) + 128.0f);

  // printf("(%d,%d,%d)=0x%08x\n", r, g, b, result);
  return result;
}

const static auto black = uyvy422(0, 0, 0);
const static auto red = uyvy422(255, 0, 0);
const static auto green = uyvy422(0, 255, 0);
const static auto white = uyvy422(255, 255, 255);

static void setArea(uint32_t *screen, int stride, int startX, int startY,
                    int width, int height, uint32_t color)
{
  // std::cout << "setArea" << width << "x" << height << " stride" << stride
  //           << " ptr" << std::hex << (void *)screen << std::dec << std::endl;
  for (int x = startX / 2; x < startX / 2 + width / 2; x++)
  {
    for (int y = startY; y < startY + height; y++)
    {
      screen[x + y * stride / 4] = color;
    }
  }
}

void encodeTimestamp(uint8_t *ptr, int stride, uint64_t ts100ns)
{
  // std::cout << "overlayTime" << std::endl;
  // return;
  uint32_t *screen = (uint32_t *)(ptr);
  setArea(screen, stride, 0, 0, 128, 3, black);
  uint64_t mask = 0x8000000000000000L;
  for (int bit = 0; bit < 64; bit++)
  {
    const bool val = (ts100ns & mask) != 0;
    mask >>= 1;
    setArea(screen, stride, bit * 2, 1, 2, 1, val ? white : black);
  }
}
