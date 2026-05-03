#include <iostream>
#include <cstring>
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

  if (frame->pixelFormat == Frame::YUV420P)
  {
    // Planar I420 crop: each plane cropped independently
    const int srcW = frame->xres;
    const int srcH = frame->yres;
    const uint8_t *srcY = frame->data;
    const uint8_t *srcU = srcY + srcW * srcH;
    const uint8_t *srcV = srcU + (srcW / 2) * (srcH / 2);
    uint8_t *dstY = croppedFrame->data;
    uint8_t *dstU = dstY + cropWidth * cropHeight;
    uint8_t *dstV = dstU + (cropWidth / 2) * (cropHeight / 2);
    for (int y = 0; y < cropHeight; ++y)
      std::memcpy(dstY + y * cropWidth, srcY + (cropY + y) * srcW + cropX, cropWidth);
    for (int y = 0; y < cropHeight / 2; ++y)
      std::memcpy(dstU + y * (cropWidth / 2), srcU + (cropY / 2 + y) * (srcW / 2) + cropX / 2, cropWidth / 2);
    for (int y = 0; y < cropHeight / 2; ++y)
      std::memcpy(dstV + y * (cropWidth / 2), srcV + (cropY / 2 + y) * (srcW / 2) + cropX / 2, cropWidth / 2);
  }
  else
  {
    int bytesPerPixel = (frame->pixelFormat == Frame::UYVY422) ? 2 : 3;
    for (int y = 0; y < cropHeight; ++y)
    {
      uint8_t *srcPtr = frame->data + ((cropY + y) * frame->stride) + (cropX * bytesPerPixel);
      uint8_t *dstPtr = croppedFrame->data + (y * croppedFrame->stride);
      std::memcpy(dstPtr, srcPtr, cropWidth * bytesPerPixel);
    }
  }

  return croppedFrame;
}
