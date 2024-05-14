
#include "VideoReader.hpp"
#include <Processing.NDI.Lib.h>
#include <atomic>
#include <chrono>
#include <cstring> // strerror
#include <iomanip>
#include <sstream>
#include <thread>

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

const uint32_t uyvy422(uint8_t r, uint8_t g, uint8_t b) {
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

  const auto border = 4;
  for (size_t y = 0; y < border; y++) {
    for (size_t x = 0; x < digitPixels[0].size() * scale + border * 2; ++x) {
      screen[(start.x + x) + (start.y + y) * stride / 4] = bg;
      screen[(start.x + x) +
             (start.y + y + digitPixels.size() * scale + border) * stride / 4] =
          bg;
    }
  }
  for (size_t y = 0; y < digitPixels.size() * scale + 2 * border; ++y) {
    for (size_t x = 0; x < border; x++) {
      screen[(start.x + x) + (start.y + y) * stride / 4] = bg;
      screen[(start.x + x + border + digitPixels[0].size() * scale) +
             (start.y + y) * stride / 4] = bg;
    }
  }
  const auto yOffset = border;
  const auto xOffset = border;
  for (size_t y = 0; y < digitPixels.size(); ++y) {
    for (size_t yExpand = 0; yExpand < scale; yExpand++) {
      for (size_t x = 0; x < digitPixels[y].size(); ++x) {
        const auto pixel = digitPixels[y][x] ? fg : bg;
        for (size_t xExpand = 0; xExpand < scale; xExpand++) {
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

void overlayTime(uint32_t *screen, int stride, uint64_t ts100ns) {
  const auto milli = (5000 + ts100ns) / 10000;

  // Convert utc milliseconds to time_point of system clock
  time_point<system_clock> tp = time_point<system_clock>(milliseconds(milli));

  // Convert to system time_t for conversion to tm structure
  std::time_t raw_time = system_clock::to_time_t(tp);

  // Convert to local time
  std::tm *local_time = std::localtime(&raw_time);

  // Extract the local hours and minutes
  int local_hours = local_time->tm_hour;
  int local_minutes = local_time->tm_min;
  int local_secs = local_time->tm_sec;

  Point point = Point(20, 40);

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
  std::shared_ptr<FrameProcessor> frameProcessor;
  NDIlib_recv_instance_t pNDI_recv;
  const std::string srcName;

  class NdiFrame : public Frame {
    NDIlib_recv_instance_t pNDI_recv;
    NDIlib_video_frame_v2_t ndiFrame;

  public:
    NdiFrame(NDIlib_recv_instance_t pNDI_recv, NDIlib_video_frame_v2_t ndiFrame)
        : pNDI_recv(pNDI_recv), ndiFrame(ndiFrame){};
    virtual ~NdiFrame() override {
      NDIlib_recv_free_video_v2(pNDI_recv, &ndiFrame);
    }
  };

  std::string open(std::shared_ptr<FrameProcessor> frameProcessor) override {
    // Create a finder
    NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
    if (!pNDI_find)
      return "NDIlib_find_create_v2() failed";

    // Wait until there is a source

    const NDIlib_source_t *p_source = NULL;

    // FIXME - allow passing in srcName
    while (!p_source) {
      SystemEventQueue::push("NDI", "Looking for source " + srcName);
      const NDIlib_source_t *p_sources = NULL;
      uint32_t no_sources = 0;
      while (!no_sources) {
        // Wait until the sources on the network have changed
        NDIlib_find_wait_for_sources(pNDI_find, 1000 /* One second */);
        p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
      }

      for (int src = 0; src < no_sources; src++) {
        SystemEventQueue::push("NDI", std::string("Source Found: ") +
                                          p_sources[src].p_ndi_name + " at " +
                                          p_sources[src].p_ip_address);
        if (std::string(p_sources[src].p_ndi_name).find(srcName) == 0) {
          p_source = p_sources + src;
        }
      }
    }

    NDIlib_recv_create_v3_t recv_create;

#ifdef NDI_BGRX
    recv_create.color_format =
        NDIlib_recv_color_format_BGRX_BGRA; // NDIlib_recv_color_format_RGBX_RGBA;
#else
    recv_create.color_format = NDIlib_recv_color_format_UYVY_BGRA;
#endif
    // We now have at least one source, so we create a receiver to look at it.
    pNDI_recv = NDIlib_recv_create_v3(&recv_create);
    if (!pNDI_recv)
      return "NDIlib_recv_create_v3() failed";

    // Connect to the source
    SystemEventQueue::push("NDI", std::string("Connecting to ") +
                                      p_source->p_ndi_name + " at " +
                                      p_source->p_ip_address);
    // Connect to our sources
    NDIlib_recv_connect(pNDI_recv, p_source);

    // Destroy the NDI finder. We needed to have access to the pointers to
    // p_sources[0]
    NDIlib_find_destroy(pNDI_find);

    this->frameProcessor = frameProcessor;

    return "";
  }

  void run() {
    int64_t lastTS = 0;
    while (keepRunning.load()) {
      NDIlib_video_frame_v2_t video_frame;
      NDIlib_audio_frame_v3_t audio_frame;

      auto frameType = NDIlib_recv_capture_v3(pNDI_recv, &video_frame,
                                              &audio_frame, nullptr, 5000);
      switch (frameType) {
      // No data
      case NDIlib_frame_type_none:
        printf("No data received.\n");
        break;

        // Video data
      case NDIlib_frame_type_video: {
        if (video_frame.xres && video_frame.yres) {
          if (video_frame.timestamp == NDIlib_recv_timestamp_undefined) {
            std::cerr << "timestamp not supported" << std::endl;
          }
          // std::cout << "Video data received (" << video_frame.xres << "x"
          //           << video_frame.yres << std::endl;
          overlayTime((uint32_t *)video_frame.p_data,
                      video_frame.line_stride_in_bytes, video_frame.timestamp);
          auto delta = video_frame.timestamp - lastTS;
          if (delta == 0 || (lastTS != 0 && delta > 400000)) {
            // Convert 100ns ticks since 1970 to a duration, then to
            // system_clock::time_point
            auto duration_since_epoch =
                std::chrono::duration<uint64_t, std::ratio<1, 10000000>>(
                    video_frame.timestamp);

            // Convert custom duration to system_clock::duration
            auto system_duration =
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    duration_since_epoch);

            // Correctly construct a system_clock::time_point from the
            // system_duration
            std::chrono::system_clock::time_point time_point(system_duration);

            // Convert to time_t for local time conversion
            std::time_t local_time_t = system_clock::to_time_t(time_point);

            // Convert to tm for formatting
            std::tm *local_tm = std::localtime(&local_time_t);
            auto milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    system_duration) %
                1000;
            std::stringstream ss;
            ss << "Gap: " << delta / 10000 << "ms at "
               << std::put_time(local_tm, "%H:%M:%S") << "." << std::setw(3)
               << std::setfill('0') << milliseconds.count() << " Local";
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

    // Destroy the receiver
    NDIlib_recv_destroy(pNDI_recv);

    // Not required, but nice
    NDIlib_destroy();
  }

public:
  NdiReader(const std::string srcName) : srcName(srcName) {}
  std::string start() override {
    keepRunning = true;
    ndiThread = std::thread([this]() { run(); });
#ifndef _WIN32
    setThreadPriority(ndiThread, SCHED_FIFO, 10);
#endif
    return "";
  };
  std::string stop() override {
    keepRunning = false;
    ndiThread.join();
    return "";
  }
};

std::shared_ptr<VideoReader> createNdiReader(std::string srcName) {
  return std::shared_ptr<NdiReader>(new NdiReader(srcName));
}
