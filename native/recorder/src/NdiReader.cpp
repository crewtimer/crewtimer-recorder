
#include "VideoReader.hpp"
#include <Processing.NDI.Lib.h>
#include <algorithm> // For std::min and std::max
#include <atomic>
#include <chrono>
#include <cstring> // For memcpy, strerror
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "SystemEventQueue.hpp"

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif // _WIN32

using namespace std::chrono;

// 64% cpu rx uyuv422, convert to bgr, save to disk
// 68% cpu bgrx to bgr save to disk
// 208% cpu bgrx to bgr save as mkv X264

/**
 * Crops a region from a UYVY422 frame buffer.
 *
 * @param uyvyBuffer   The pointer to the original UYVY422 frame buffer.
 * @param frameWidth   The width of the original frame in pixels.
 * @param frameHeight  The height of the original frame in pixels.
 * @param x            The x-coordinate of the top-left corner of the crop
 * region.
 * @param y            The y-coordinate of the top-left corner of the crop
 * region.
 * @param width        The width of the crop region in pixels.
 * @param height       The height of the crop region in pixels.
 * @param lineStride   The number of bytes in each row of the original frame
 * buffer.
 * @return             A vector containing the cropped frame buffer in UYVY422
 * format. If the crop region is invalid, returns an empty vector.
 */
std::vector<uint8_t> cropUYVY422Frame(const uint8_t *uyvyBuffer, int frameWidth,
                                      int frameHeight, int x, int y, int width,
                                      int height, int lineStride) {
  int bytesPerPixel =
      2; // Each pixel in UYVY422 is 2 bytes (4 bytes for 2 pixels)

  // Adjust the width and height to ensure they are within frame bounds
  int maxWidth = frameWidth - x;
  int maxHeight = frameHeight - y;
  width = std::max(0, std::min(width, maxWidth));
  height = std::max(0, std::min(height, maxHeight));

  // Check if the requested region is valid
  if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= frameWidth ||
      y >= frameHeight) {
    std::cerr << "Invalid crop region." << std::endl;
    return {};
  }

  // Create a buffer for the cropped frame
  std::vector<uint8_t> croppedBuffer(width * height * bytesPerPixel);

  // Iterate over the rows in the cropping region
  for (int row = 0; row < height; ++row) {
    // Calculate the source position
    int srcY = y + row;
    int srcX = x * bytesPerPixel;

    // Calculate the source offset in the original buffer with the lineStride
    const uint8_t *srcPtr = uyvyBuffer + srcY * lineStride + srcX;

    // Calculate the destination position in the cropped buffer
    uint8_t *destPtr = croppedBuffer.data() + row * width * bytesPerPixel;

    // Copy the row into the cropped buffer
    std::memcpy(destPtr, srcPtr, width * bytesPerPixel);
  }

  return croppedBuffer;
}

uint32_t uyvy422(uint8_t r, uint8_t g, uint8_t b) {
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

auto timeColor = uyvy422(0, 255, 0);
const auto black = uyvy422(0, 0, 0);
const auto red = uyvy422(255, 0, 0);
const auto green = uyvy422(0, 255, 0);
const auto white = uyvy422(255, 255, 255);
auto scale = 3;

// Define pixel representations for each digit (0-9)
std::vector<std::vector<std::vector<int>>> digits = {
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

class Point {
public:
  int x;
  int y;

  // Constructor
  Point(int xCoordinate, int yCoordinate) {
    x = xCoordinate;
    y = yCoordinate;
  }
};

void setDigitPixels(uint32_t *screen, int digit, Point &start, int stride,
                    uint32_t fg, uint32_t bg) {
  std::vector<std::vector<int>> &digitPixels =
      digits[digit]; // Get the pixel representation for the digit

  const size_t border = 4;
  for (size_t y = 0; y < border; y++) {
    for (size_t x = 0; x < size_t(digitPixels[0].size() * scale + border * 2);
         ++x) {
      screen[(start.x + x) + (start.y + y) * stride / 4] = bg;
      screen[(start.x + x) +
             (start.y + y + digitPixels.size() * scale + border) * stride / 4] =
          bg;
    }
  }
  for (size_t y = 0; y < size_t(digitPixels.size() * scale + 2 * border); ++y) {
    for (size_t x = 0; x < border; x++) {
      screen[(start.x + x) + (start.y + y) * stride / 4] = bg;
      screen[(start.x + x + border + digitPixels[0].size() * scale) +
             (start.y + y) * stride / 4] = bg;
    }
  }
  const auto yOffset = border;
  const auto xOffset = border;
  for (size_t y = 0; y < digitPixels.size(); ++y) {
    for (int yExpand = 0; yExpand < scale; yExpand++) {
      for (size_t x = 0; x < digitPixels[y].size(); ++x) {
        const auto pixel = digitPixels[y][x] ? fg : bg;
        for (int xExpand = 0; xExpand < scale; xExpand++) {
          screen[(xOffset + start.x + x * scale + xExpand) +
                 ((yOffset + start.y) + y * scale + yExpand) * stride / 4] =
              pixel;
        }
      }
    }
  }
  start.x += int(digitPixels[0].size() * scale + border * 2 - 2);
}

void setArea(uint32_t *screen, int stride, int startX, int startY, int width,
             int height, uint32_t color) {
  for (int x = startX / 2; x < startX / 2 + width / 2; x++) {
    for (int y = startY; y < startY + height; y++) {
      screen[x + y * stride / 4] = color;
    }
  }
}

void overlayDigits(uint32_t *screen, int stride, Point &point, uint16_t value,
                   int digits) {
  // clearArea(screen, stride, point.x - scale * 2, point.y - scale * 4,
  //           digits * (3 * scale + 2 * scale) + scale*4,
  //           5 * scale + scale * 6);

  if (digits >= 3) {
    const auto hundreds = (value / 100) % 10;
    setDigitPixels(screen, hundreds, point, stride, timeColor, black);
  }
  if (digits >= 2) {
    const auto tens = (value / 10) % 10;
    setDigitPixels(screen, tens, point, stride, timeColor, black);
  }

  const auto ones = value % 10;
  setDigitPixels(screen, ones, point, stride, timeColor, black);
}

void overlayTime(uint32_t *screen, int stride, uint64_t ts100ns,
                 const std::tm *local_time) {
  // Extract the local hours and minutes
  // int local_hours = local_time->tm_hour;
  // int local_minutes = local_time->tm_min;
  // int local_secs = local_time->tm_sec;
  // const auto milli = (5000 + ts100ns) / 10000;

  // Point point = Point(20, 40);

  // overlayDigits(screen, stride, point, local_hours, 2);
  // setDigitPixels(screen, 10, point, stride, timeColor, black);
  // overlayDigits(screen, stride, point, local_minutes, 2);
  // setDigitPixels(screen, 10, point, stride, timeColor, black);
  // overlayDigits(screen, stride, point, local_secs, 2);
  // setDigitPixels(screen, 11, point, stride, timeColor, black);
  // overlayDigits(screen, stride, point, milli % 1000, 3);

  // std::cout << "Current time: " << local_hours << ":" << local_minutes << ":"
  //           << local_secs << std::endl;
  // std::cout << "Calced  time: " << (milli / (60 * 60 * 1000)) % 24 << ":"
  //           << (milli / (60 * 1000)) % 60 << ":" << (milli / (1000)) % 60
  //           << " m=" << milli << std::endl;

  setArea(screen, stride, 0, 0, 128, 3, black);
  uint64_t mask = 0x8000000000000000L;
  for (int bit = 0; bit < 64; bit++) {
    const bool val = (ts100ns & mask) != 0;
    mask >>= 1;
    setArea(screen, stride, bit * 2, 1, 2, 1, val ? white : black);
  }
}

#ifndef _WIN32
void setThreadPriority(std::thread &thread, int policy, int priority) {
  pthread_t threadID = thread.native_handle();
  sched_param sch_params;
  sch_params.sched_priority = priority;
  if (pthread_setschedparam(threadID, policy, &sch_params)) {
    std::cerr << "Failed to set Thread scheduling : " << std::strerror(errno)
              << std::endl;
  }
}
#endif

class NdiReader : public VideoReader {
  std::thread ndiThread;
  std::atomic<bool> keepRunning;
  std::atomic<bool> scanEnabled;
  std::shared_ptr<FrameProcessor> frameProcessor;
  NDIlib_recv_instance_t pNDI_recv = nullptr;
  NDIlib_find_instance_t pNDI_find = nullptr;
  std::string srcName;

  std::vector<CameraInfo> camList;
  std::thread scanThread;
  std::mutex scanMutex;

  class NdiFrame : public Frame {
    NDIlib_recv_instance_t pNDI_recv;
    NDIlib_video_frame_v2_t ndiFrame;

  public:
    NdiFrame(NDIlib_recv_instance_t pNDI_recv, NDIlib_video_frame_v2_t ndiFrame)
        : pNDI_recv(pNDI_recv), ndiFrame(ndiFrame){

                                };
    virtual ~NdiFrame() override {
      if (pNDI_recv) {
        NDIlib_recv_free_video_v2(pNDI_recv, &ndiFrame);
      }
    }
  };

  std::vector<CameraInfo> getCameraList() override {
    std::unique_lock<std::mutex> lock(scanMutex);
    return camList;
  }

  /**
   * @brief Search for NDI Sources.  This should always be called from the same
   * thread or defuct threads may cause issues when the program terminates.
   *
   * @return std::vector<CameraInfo>
   */
  std::vector<CameraInfo> findCameras() {
    std::vector<CameraInfo> list;
    // Create a finder
    if (pNDI_find == nullptr) {
      pNDI_find = NDIlib_find_create_v2();
    }

    if (!pNDI_find)
      return list;

    const NDIlib_source_t *p_sources = NULL;
    uint32_t no_sources = 0;
    // Wait until the sources on the network have changed
    NDIlib_find_wait_for_sources(pNDI_find, 2000);
    p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

    for (uint32_t src = 0; src < no_sources; src++) {
      list.push_back(
          CameraInfo(p_sources[src].p_ndi_name, p_sources[src].p_url_address));
      // SystemEventQueue::push("NDI", std::string("Source Found: ") +
      //                                   p_sources[src].p_ndi_name + " at "
      //                                   + p_sources[src].p_ip_address);
    }

    return list;
  };

  void scanLoop() {
    std::cout << "Scan loop started" << std::endl;
    while (scanEnabled) {
      auto list = findCameras();
      {
        std::unique_lock<std::mutex> lock(scanMutex);
        camList = list;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
    if (pNDI_find != nullptr) {
      NDIlib_find_destroy(pNDI_find);
      pNDI_find = nullptr;
    }
    std::cout << "Scan loop stopped" << std::endl;
  }

  std::string connect() {
    SystemEventQueue::push("NDI", "Searching for NDI sources...");
    NDIlib_source_t p_source;
    CameraInfo foundCamera;
    std::vector<CameraInfo> cameras;
    while (foundCamera.name == "" && keepRunning) {
      std::unique_lock<std::mutex> lock(scanMutex);
      cameras = camList;
      for (auto camera : cameras) {
        // std::cout << "Found camera: " << camera.name << std::endl;
        if (camera.name.find(srcName) == 0) {
          foundCamera = camera;
          break;
        }
      }
    }

    if (foundCamera.name == "") {
      return ""; // stop received before ndi source found
    }

    NDIlib_recv_create_v3_t recv_create;

    recv_create.color_format = NDIlib_recv_color_format_UYVY_BGRA;
    // We now have at least one source, so we create a receiver to look at it.
    pNDI_recv = NDIlib_recv_create_v3(&recv_create);
    if (!pNDI_recv)
      return "NDIlib_recv_create_v3() failed";

    // Connect to the source
    SystemEventQueue::push("NDI", std::string("Connecting to ") +
                                      foundCamera.name + " at " +
                                      foundCamera.address);
    // Connect to our sources
    p_source.p_ndi_name = foundCamera.name.c_str();
    p_source.p_url_address = foundCamera.address.c_str();
    NDIlib_recv_connect(pNDI_recv, &p_source);

    foundCamera.name = "";
    cameras.clear();

    return "";
  }

  void run() {
    int64_t lastTS = 0;
    int64_t frameCount = 0;
    connect();
    while (keepRunning) {
      NDIlib_video_frame_v2_t video_frame;
      NDIlib_audio_frame_v3_t audio_frame;

      auto frameType = NDIlib_recv_capture_v3(pNDI_recv, &video_frame, nullptr,
                                              nullptr, 5000);
      switch (frameType) {
      // No data
      case NDIlib_frame_type_none:
        printf("No data received.\n");
        break;

        // Video data
      case NDIlib_frame_type_video: {
        if (video_frame.xres && video_frame.yres) {
          frameCount++;
          if (frameCount == 1) {
            break; // 1st frame often old frame cached from ndi sender.
                   // Ignore.
          }
          if (video_frame.timestamp == NDIlib_recv_timestamp_undefined) {
            std::cerr << "timestamp not supported" << std::endl;
          }
          // std::cout << "Video data received (" << video_frame.xres << "x"
          //           << video_frame.yres << std::endl;

          auto ts100ns = video_frame.timestamp;
          const auto milli = (5000 + ts100ns) / 10000;

          // Convert utc milliseconds to time_point of system clock
          time_point<system_clock> tp =
              time_point<system_clock>(milliseconds(milli));

          // Convert to system time_t for conversion to tm structure
          std::time_t raw_time = system_clock::to_time_t(tp);

          // Convert to local time
          std::tm *local_time = std::localtime(&raw_time);
          overlayTime((uint32_t *)video_frame.p_data,
                      video_frame.line_stride_in_bytes, video_frame.timestamp,
                      local_time);
          auto delta = video_frame.timestamp - lastTS;

          auto msPerFrame =
              1000 * video_frame.frame_rate_D / video_frame.frame_rate_N;
          if (delta == 0 || (lastTS != 0 && delta >= 10000 * 2 * msPerFrame)) {
            std::stringstream ss;
            ss << "Gap: " << delta / 10000 << "ms at "
               << std::put_time(local_time, "%H:%M:%S") << "." << std::setw(3)
               << std::setfill('0') << milli % 1000 << " Local";
            SystemEventQueue::push("NDI", ss.str());
          }

          lastTS = video_frame.timestamp;
          auto txframe = std::make_shared<NdiFrame>(pNDI_recv, video_frame);
          txframe->xres = video_frame.xres & ~1; // force even
          txframe->yres = video_frame.yres & ~1;
          txframe->stride = video_frame.line_stride_in_bytes;
          txframe->timestamp = video_frame.timestamp;
          txframe->data = video_frame.p_data;
          txframe->frame_rate_N = video_frame.frame_rate_N;
          txframe->frame_rate_D = video_frame.frame_rate_D;
          txframe->pixelFormat = Frame::PixelFormat::UYVY422;
          frameProcessor->addFrame(txframe);
        } else {
          NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
        }
      } break;

        // Audio data
      case NDIlib_frame_type_audio: {
        // printf("Audio data received (%d samples @%d fs).\n",
        // audio_frame.no_samples, audio_frame.sample_rate);
        // auto level = detectTone((int *)audio_frame.p_data,
        // audio_frame.no_samples,
        //                         audio_frame.sample_rate, 400);

        NDIlib_recv_free_audio_v3(pNDI_recv, &audio_frame);
      } break;
      default:
        break;
      }
    }
  }

public:
  NdiReader() {
    scanEnabled = true;
    scanThread = std::thread(&NdiReader::scanLoop, this);
    auto *version = NDIlib_version();
    std::cout << "NDI SDK Version: " << version << std::endl;
  }
  std::string start(const std::string srcName,
                    std::shared_ptr<FrameProcessor> frameProcessor) override {
    this->srcName = srcName;
    this->frameProcessor = frameProcessor;
    keepRunning = true;
    ndiThread = std::thread(&NdiReader::run, this);
#ifndef _WIN32
    setThreadPriority(ndiThread, SCHED_FIFO, 10);
#endif
    return "";
  };
  std::string stop() override {
    keepRunning = false;
    if (ndiThread.joinable()) {
      ndiThread.join();
    }
    if (frameProcessor) {
      frameProcessor = nullptr;
    }

    // Destroy the receiver
    if (pNDI_recv) {
      NDIlib_recv_connect(pNDI_recv, nullptr);
      // disconnect
      NDIlib_recv_destroy(pNDI_recv);
      pNDI_recv = nullptr;
    }
    return "";
  }
  virtual ~NdiReader() override {
    stop();
    scanEnabled = false;
    if (scanThread.joinable()) {
      scanThread.join();
    }

    // Not required, but nice
    NDIlib_destroy();
  };
};

std::shared_ptr<VideoReader> createNdiReader() {
  return std::shared_ptr<NdiReader>(new NdiReader());
}
