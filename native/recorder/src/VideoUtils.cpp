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
  croppedFrame->sensorXres = frame->sensorXres;
  croppedFrame->sensorYres = frame->sensorYres;
  croppedFrame->rotation = frame->rotation;

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

static void copyFrameProperties(const FramePtr &source, const FramePtr &destination,
                                bool clockwise)
{
  destination->timestamp = source->timestamp;
  destination->frame_rate_N = source->frame_rate_N;
  destination->frame_rate_D = source->frame_rate_D;
  destination->sensorXres = source->sensorXres ? source->sensorXres : source->xres;
  destination->sensorYres = source->sensorYres ? source->sensorYres : source->yres;
  destination->rotation = source->rotation + (clockwise ? 90 : -90);
}

static void rotatePlane90(const uint8_t *source, int sourceWidth, int sourceHeight,
                          int sourceStride, uint8_t *destination,
                          int destinationStride, bool clockwise)
{
  for (int y = 0; y < sourceHeight; ++y)
  {
    for (int x = 0; x < sourceWidth; ++x)
    {
      const int destinationX = clockwise ? sourceHeight - 1 - y : y;
      const int destinationY = clockwise ? x : sourceWidth - 1 - x;
      destination[destinationY * destinationStride + destinationX] =
          source[y * sourceStride + x];
    }
  }
}

FramePtr rotateFrame90(const FramePtr &frame, bool clockwise)
{
  if (!frame || frame->xres <= 0 || frame->yres <= 0)
    return nullptr;

  auto rotated = std::make_shared<Frame>(frame->yres, frame->xres, frame->pixelFormat);
  copyFrameProperties(frame, rotated, clockwise);

  if (frame->pixelFormat == Frame::YUV420P)
  {
    const int sourceWidth = frame->xres;
    const int sourceHeight = frame->yres;
    const int destinationWidth = rotated->xres;
    const uint8_t *sourceY = frame->data;
    const uint8_t *sourceU = sourceY + sourceWidth * sourceHeight;
    const uint8_t *sourceV = sourceU + (sourceWidth / 2) * (sourceHeight / 2);
    uint8_t *destinationY = rotated->data;
    uint8_t *destinationU = destinationY + rotated->xres * rotated->yres;
    uint8_t *destinationV = destinationU + (rotated->xres / 2) * (rotated->yres / 2);
    rotatePlane90(sourceY, sourceWidth, sourceHeight, sourceWidth,
                  destinationY, destinationWidth, clockwise);
    rotatePlane90(sourceU, sourceWidth / 2, sourceHeight / 2, sourceWidth / 2,
                  destinationU, destinationWidth / 2, clockwise);
    rotatePlane90(sourceV, sourceWidth / 2, sourceHeight / 2, sourceWidth / 2,
                  destinationV, destinationWidth / 2, clockwise);
  }
  else if (frame->pixelFormat == Frame::UYVY422)
  {
    // Repack each output pixel pair. Chroma is averaged because source pixels
    // adjacent after rotation came from different horizontal UYVY pairs.
    for (int destinationY = 0; destinationY < rotated->yres; ++destinationY)
    {
      auto *destination = rotated->data + destinationY * rotated->stride;
      for (int destinationX = 0; destinationX < rotated->xres; destinationX += 2)
      {
        uint8_t luma[2];
        uint16_t chromaU = 0;
        uint16_t chromaV = 0;
        for (int i = 0; i < 2; ++i)
        {
          const int outputX = destinationX + i;
          const int sourceX = clockwise ? destinationY : frame->xres - 1 - destinationY;
          const int sourceY = clockwise ? frame->yres - 1 - outputX : outputX;
          const uint8_t *sourcePair =
              frame->data + sourceY * frame->stride + (sourceX / 2) * 4;
          luma[i] = sourcePair[(sourceX % 2) ? 3 : 1];
          chromaU += sourcePair[0];
          chromaV += sourcePair[2];
        }
        destination[0] = static_cast<uint8_t>(chromaU / 2);
        destination[1] = luma[0];
        destination[2] = static_cast<uint8_t>(chromaV / 2);
        destination[3] = luma[1];
        destination += 4;
      }
    }
  }
  else
  {
    constexpr int bytesPerPixel = 3;
    for (int y = 0; y < frame->yres; ++y)
    {
      for (int x = 0; x < frame->xres; ++x)
      {
        const int destinationX = clockwise ? frame->yres - 1 - y : y;
        const int destinationY = clockwise ? x : frame->xres - 1 - x;
        std::memcpy(rotated->data + destinationY * rotated->stride +
                        destinationX * bytesPerPixel,
                    frame->data + y * frame->stride + x * bytesPerPixel,
                    bytesPerPixel);
      }
    }
  }

  return rotated;
}
