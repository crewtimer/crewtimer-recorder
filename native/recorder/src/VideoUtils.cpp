#include <iostream>
#include <algorithm>
#include <cstddef>
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
  croppedFrame->receivedTs100ns = frame->receivedTs100ns;
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
  destination->receivedTs100ns = source->receivedTs100ns;
  destination->frame_rate_N = source->frame_rate_N;
  destination->frame_rate_D = source->frame_rate_D;
  destination->sensorXres = source->sensorXres ? source->sensorXres : source->xres;
  destination->sensorYres = source->sensorYres ? source->sensorYres : source->yres;
  destination->rotation = source->rotation + (clockwise ? 90 : -90);
}

// Square tiles keep both the row-wise reads and column-wise writes of a
// rotation within cache. Must be even so UYVY/I420 2x2 blocks stay whole.
static constexpr int kRotateTile = 32;

static void rotatePlane90(const uint8_t *source, int sourceWidth, int sourceHeight,
                          int sourceStride, uint8_t *destination,
                          int destinationStride, bool clockwise)
{
  for (int tileY = 0; tileY < sourceHeight; tileY += kRotateTile)
  {
    const int endY = std::min(tileY + kRotateTile, sourceHeight);
    for (int tileX = 0; tileX < sourceWidth; tileX += kRotateTile)
    {
      const int endX = std::min(tileX + kRotateTile, sourceWidth);
      for (int y = tileY; y < endY; ++y)
      {
        for (int x = tileX; x < endX; ++x)
        {
          const int destinationX = clockwise ? sourceHeight - 1 - y : y;
          const int destinationY = clockwise ? x : sourceWidth - 1 - x;
          destination[destinationY * destinationStride + destinationX] =
              source[y * sourceStride + x];
        }
      }
    }
  }
}

FramePtr rotateUyvyToI420(const FramePtr &frame, int rotation)
{
  if (!frame || frame->pixelFormat != Frame::UYVY422 || frame->xres <= 0 ||
      frame->yres <= 0 || (frame->xres % 2) || (frame->yres % 2) ||
      (rotation != 90 && rotation != -90 && rotation != -180 && rotation != 180))
    return nullptr;

  const int width = frame->xres;
  const int height = frame->yres;
  const bool quarterTurn = rotation != -180 && rotation != 180;
  const int destinationWidth = quarterTurn ? height : width;
  const int destinationHeight = quarterTurn ? width : height;
  auto rotated = std::make_shared<Frame>(destinationWidth, destinationHeight,
                                         Frame::YUV420P);
  rotated->timestamp = frame->timestamp;
  rotated->receivedTs100ns = frame->receivedTs100ns;
  rotated->frame_rate_N = frame->frame_rate_N;
  rotated->frame_rate_D = frame->frame_rate_D;
  rotated->sensorXres = frame->sensorXres ? frame->sensorXres : width;
  rotated->sensorYres = frame->sensorYres ? frame->sensorYres : height;
  rotated->rotation = frame->rotation + (rotation == 180 ? -180 : rotation);

  // Express the rotation as a destination origin plus the offset moved per
  // source pixel step in x and in y, for luma and for 2x2-block chroma.
  const ptrdiff_t lumaStride = destinationWidth;
  const ptrdiff_t chromaStride = destinationWidth / 2;
  ptrdiff_t lumaOrigin, lumaStepX, lumaStepY;
  ptrdiff_t chromaOrigin, chromaStepX, chromaStepY;
  if (rotation == 90)
  {
    lumaOrigin = height - 1;
    lumaStepX = lumaStride;
    lumaStepY = -1;
    chromaOrigin = height / 2 - 1;
    chromaStepX = chromaStride;
    chromaStepY = -1;
  }
  else if (rotation == -90)
  {
    lumaOrigin = (width - 1) * lumaStride;
    lumaStepX = -lumaStride;
    lumaStepY = 1;
    chromaOrigin = (width / 2 - 1) * chromaStride;
    chromaStepX = -chromaStride;
    chromaStepY = 1;
  }
  else
  {
    lumaOrigin = (height - 1) * lumaStride + width - 1;
    lumaStepX = -1;
    lumaStepY = -lumaStride;
    chromaOrigin = (height / 2 - 1) * chromaStride + width / 2 - 1;
    chromaStepX = -1;
    chromaStepY = -chromaStride;
  }

  uint8_t *destinationY = rotated->data;
  uint8_t *destinationU = destinationY + destinationWidth * destinationHeight;
  uint8_t *destinationV =
      destinationU + (destinationWidth / 2) * (destinationHeight / 2);

  // Each 2x2 source block holds one UYVY chroma pair per row; their average
  // is exactly the 4:2:0 sample for the rotated 2x2 destination block.
  for (int tileY = 0; tileY < height; tileY += kRotateTile)
  {
    const int endY = std::min(tileY + kRotateTile, height);
    for (int tileX = 0; tileX < width; tileX += kRotateTile)
    {
      const int endX = std::min(tileX + kRotateTile, width);
      for (int y = tileY; y < endY; y += 2)
      {
        const uint8_t *row0 = frame->data + y * frame->stride;
        const uint8_t *row1 = row0 + frame->stride;
        for (int x = tileX; x < endX; x += 2)
        {
          const uint8_t *pair0 = row0 + x * 2;
          const uint8_t *pair1 = row1 + x * 2;
          uint8_t *luma = destinationY + lumaOrigin + x * lumaStepX + y * lumaStepY;
          luma[0] = pair0[1];
          luma[lumaStepX] = pair0[3];
          luma[lumaStepY] = pair1[1];
          luma[lumaStepX + lumaStepY] = pair1[3];
          const ptrdiff_t chroma =
              chromaOrigin + (x / 2) * chromaStepX + (y / 2) * chromaStepY;
          destinationU[chroma] = static_cast<uint8_t>((pair0[0] + pair1[0] + 1) >> 1);
          destinationV[chroma] = static_cast<uint8_t>((pair0[2] + pair1[2] + 1) >> 1);
        }
      }
    }
  }

  return rotated;
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

FramePtr rotateFrame180(const FramePtr &frame)
{
  if (!frame || frame->xres <= 0 || frame->yres <= 0)
    return nullptr;

  auto rotated =
      std::make_shared<Frame>(frame->xres, frame->yres, frame->pixelFormat);
  rotated->timestamp = frame->timestamp;
  rotated->receivedTs100ns = frame->receivedTs100ns;
  rotated->frame_rate_N = frame->frame_rate_N;
  rotated->frame_rate_D = frame->frame_rate_D;
  rotated->sensorXres = frame->sensorXres ? frame->sensorXres : frame->xres;
  rotated->sensorYres = frame->sensorYres ? frame->sensorYres : frame->yres;
  rotated->rotation = frame->rotation - 180;

  if (frame->pixelFormat == Frame::YUV420P)
  {
    const int yPlaneSize = frame->xres * frame->yres;
    const int chromaPlaneSize = (frame->xres / 2) * (frame->yres / 2);
    const uint8_t *sourcePlanes[] = {
        frame->data,
        frame->data + yPlaneSize,
        frame->data + yPlaneSize + chromaPlaneSize};
    uint8_t *destinationPlanes[] = {
        rotated->data,
        rotated->data + yPlaneSize,
        rotated->data + yPlaneSize + chromaPlaneSize};
    const int planeSizes[] = {yPlaneSize, chromaPlaneSize, chromaPlaneSize};
    for (int plane = 0; plane < 3; ++plane)
    {
      for (int i = 0; i < planeSizes[plane]; ++i)
        destinationPlanes[plane][i] =
            sourcePlanes[plane][planeSizes[plane] - 1 - i];
    }
  }
  else if (frame->pixelFormat == Frame::UYVY422)
  {
    for (int destinationY = 0; destinationY < frame->yres; ++destinationY)
    {
      uint8_t *destination =
          rotated->data + destinationY * rotated->stride;
      const uint8_t *sourceRow =
          frame->data + (frame->yres - 1 - destinationY) * frame->stride;
      for (int destinationX = 0; destinationX < frame->xres;
           destinationX += 2)
      {
        const int sourcePairX = frame->xres - 2 - destinationX;
        const uint8_t *source = sourceRow + sourcePairX * 2;
        destination[0] = source[0];
        destination[1] = source[3];
        destination[2] = source[2];
        destination[3] = source[1];
        destination += 4;
      }
    }
  }
  else
  {
    constexpr int bytesPerPixel = 3;
    for (int destinationY = 0; destinationY < frame->yres; ++destinationY)
    {
      for (int destinationX = 0; destinationX < frame->xres; ++destinationX)
      {
        const int sourceX = frame->xres - 1 - destinationX;
        const int sourceY = frame->yres - 1 - destinationY;
        std::memcpy(rotated->data + destinationY * rotated->stride +
                        destinationX * bytesPerPixel,
                    frame->data + sourceY * frame->stride +
                        sourceX * bytesPerPixel,
                    bytesPerPixel);
      }
    }
  }

  return rotated;
}
