#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
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
const static auto timeColor = uyvy422(0, 255, 0);
const static auto scale = 6;

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

// Define pixel representations for each digit (0-9)
const std::vector<std::vector<std::vector<int>>> digits = {
    // Digit 0
    {{1, 1, 1}, {1, 0, 1}, {1, 0, 1}, {1, 0, 1}, {1, 1, 1}},
    // Digit 1
    {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}},
    // Digit 2
    {{1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {1, 0, 0}, {1, 1, 1}},
    // Digit 3
    {{1, 1, 1}, {0, 0, 1}, {0, 1, 1}, {0, 0, 1}, {1, 1, 1}},
    // Digit 4
    {{1, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {0, 0, 1}},
    // Digit 5
    {{1, 1, 1}, {1, 0, 0}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}},
    // Digit 6
    {{1, 1, 1}, {1, 0, 0}, {1, 1, 1}, {1, 0, 1}, {1, 1, 1}},
    // Digit 7
    {{1, 1, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}},
    // Digit 8
    {{1, 1, 1}, {1, 0, 1}, {1, 1, 1}, {1, 0, 1}, {1, 1, 1}},
    // Digit 9
    {{1, 1, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {0, 0, 1}},
    // Colon
    {{0}, {1}, {0}, {1}, {0}},
    // Period
    {{0}, {0}, {0}, {0}, {1}}};

class Point
{
public:
  int x;
  int y;

  // Constructor
  Point(int xCoordinate, int yCoordinate)
  {
    x = xCoordinate;
    y = yCoordinate;
  }
};

void setDigitPixels(uint32_t *screen, int digit, Point &start, int stride,
                    uint32_t fg, uint32_t bg)
{
  const std::vector<std::vector<int>> &digitPixels =
      digits[digit]; // Get the pixel representation for the digit

  const size_t border = 4;
  for (size_t y = 0; y < border; y++)
  {
    for (size_t x = 0; x < size_t(digitPixels[0].size() * scale + border * 2);
         ++x)
    {
      screen[(start.x + x) + (start.y + y) * stride / 4] = bg;
      screen[(start.x + x) +
             (start.y + y + digitPixels.size() * scale + border) * stride / 4] =
          bg;
    }
  }
  for (size_t y = 0; y < size_t(digitPixels.size() * scale + 2 * border); ++y)
  {
    for (size_t x = 0; x < border; x++)
    {
      screen[(start.x + x) + (start.y + y) * stride / 4] = bg;
      screen[(start.x + x + border + digitPixels[0].size() * scale) +
             (start.y + y) * stride / 4] = bg;
    }
  }
  const auto yOffset = border;
  const auto xOffset = border;
  for (size_t y = 0; y < digitPixels.size(); ++y)
  {
    for (int yExpand = 0; yExpand < scale; yExpand++)
    {
      for (size_t x = 0; x < digitPixels[y].size(); ++x)
      {
        const auto pixel = digitPixels[y][x] ? fg : bg;
        for (int xExpand = 0; xExpand < scale; xExpand++)
        {
          screen[(xOffset + start.x + x * scale + xExpand) +
                 ((yOffset + start.y) + y * scale + yExpand) * stride / 4] =
              pixel;
        }
      }
    }
  }
  start.x += int(digitPixels[0].size() * scale + border * 2 - 2);
}

void overlayDigits(uint32_t *screen, int stride, Point &point, uint16_t value,
                   int digits)
{
  // clearArea(screen, stride, point.x - scale * 2, point.y - scale * 4,
  //           digits * (3 * scale + 2 * scale) + scale*4,
  //           5 * scale + scale * 6);

  if (digits >= 3)
  {
    const auto hundreds = (value / 100) % 10;
    setDigitPixels(screen, hundreds, point, stride, timeColor, black);
  }
  if (digits >= 2)
  {
    const auto tens = (value / 10) % 10;
    setDigitPixels(screen, tens, point, stride, timeColor, black);
  }

  const auto ones = value % 10;
  setDigitPixels(screen, ones, point, stride, timeColor, black);
}

void overlayTime(uint8_t *ptr, int stride, uint64_t ts100ns, const std::tm *local_time)
{
  uint32_t *screen = (uint32_t *)(ptr);
  const auto milli = (5000 + ts100ns) / 10000;
  if (local_time == nullptr)
  {
    // Convert utc milliseconds to time_point of system clock
    std::chrono::time_point<std::chrono::system_clock> tp =
        std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(milli));

    // Convert to system time_t for conversion to tm structure
    std::time_t raw_time = std::chrono::system_clock::to_time_t(tp);

    // Convert to local time
    local_time = std::localtime(&raw_time);
  }
  // Extract the local hours and minutes
  int local_hours = local_time->tm_hour;
  int local_minutes = local_time->tm_min;
  int local_secs = local_time->tm_sec;

  Point point = Point(40, 40); // Point(stride / 8, 40);

  overlayDigits(screen, stride, point, local_hours, 2);
  setDigitPixels(screen, 10, point, stride, timeColor, black);
  overlayDigits(screen, stride, point, local_minutes, 2);
  setDigitPixels(screen, 10, point, stride, timeColor, black);
  overlayDigits(screen, stride, point, local_secs, 2);
  setDigitPixels(screen, 11, point, stride, timeColor, black);
  overlayDigits(screen, stride, point, milli % 1000, 3);

  // std::cout << "Current time: " << local_hours << ":" << local_minutes << ":"
  //           << local_secs << std::endl;
  // std::cout << "Calced  time: " << (milli / (60 * 60 * 1000)) % 24 << ":"
  //           << (milli / (60 * 1000)) % 60 << ":" << (milli / (1000)) % 60
  //           << " m=" << milli << std::endl;
}
