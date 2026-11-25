#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>

#include "SystemEventQueue.hpp"
#include "VideoReader.hpp"

/**
 * Logs frame to frame camera intervals 2 ms or more from the declared frame rate.
 * The first is logged at once; later ones are counted and summarised once a minute so a
 * continuously irregular source does not flood the log. Fed every frame a reader
 * delivers, so it covers preview as well as recording, for every source type.
 */
class FrameIntervalMonitor
{
public:
  void addFrame(const FramePtr &frame)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (frame->frameType == Frame::FrameType::SOURCE_DISCONNECTED)
    {
      resetLocked();
      return;
    }
    if (frame->frameType != Frame::FrameType::VIDEO || frame->xres == 0 || frame->yres == 0)
      return;

    const float fps = frame->frame_rate_D ? float(frame->frame_rate_N) / float(frame->frame_rate_D) : 0;
    // A format change or a timestamp going backwards starts afresh.
    const bool formatChange = fps != prevFps || frame->xres != prevXres || frame->yres != prevYres;
    if (prevTs != 0 && !formatChange && fps > 0 && frame->timestamp > prevTs)
    {
      const int64_t interval100ns = int64_t(frame->timestamp - prevTs);
      const double expected100ns = 1e7 / fps;
      if (std::abs(interval100ns - expected100ns) >= tolerance100ns)
      {
        const auto now = std::chrono::steady_clock::now();
        if (pending == 0 && now - lastWarn >= warnPeriod)
        {
          std::stringstream msg;
          msg << std::fixed << std::setprecision(2) << "Warning: Frame interval "
              << interval100ns / 1e4 << " ms, expected " << expected100ns / 1e4 << " ms at "
              << fps << " fps";
          SystemEventQueue::push("VID", msg.str());
          lastWarn = now;
        }
        else
        {
          if (pending == 0)
          {
            pendingMin100ns = interval100ns;
            pendingMax100ns = interval100ns;
          }
          pendingMin100ns = std::min(pendingMin100ns, interval100ns);
          pendingMax100ns = std::max(pendingMax100ns, interval100ns);
          pendingExpectedMs = float(expected100ns / 1e4);
          pending++;
        }
      }
    }
    prevTs = frame->timestamp;
    prevFps = fps;
    prevXres = frame->xres;
    prevYres = frame->yres;
    if (std::chrono::steady_clock::now() - lastWarn >= warnPeriod)
      flushLocked();
  }

  /// Log any pending summary and forget the previous frame, e.g. when a reader starts or stops.
  void reset()
  {
    std::lock_guard<std::mutex> lock(mutex);
    resetLocked();
  }

private:
  static constexpr int64_t tolerance100ns = 20000; // 2 ms
  static constexpr std::chrono::seconds warnPeriod{60};

  std::mutex mutex;
  uint64_t prevTs = 0;
  float prevFps = 0;
  int prevXres = 0;
  int prevYres = 0;
  std::chrono::steady_clock::time_point lastWarn = std::chrono::steady_clock::now() - warnPeriod;
  uint32_t pending = 0;
  int64_t pendingMin100ns = 0;
  int64_t pendingMax100ns = 0;
  float pendingExpectedMs = 0;

  void resetLocked()
  {
    flushLocked();
    prevTs = 0;
  }

  void flushLocked()
  {
    if (pending == 0)
      return;
    const auto now = std::chrono::steady_clock::now();
    std::stringstream msg;
    msg << std::fixed << std::setprecision(2) << "Warning: " << pending
        << " more frame intervals off by 2 ms or more in the last "
        << std::chrono::duration_cast<std::chrono::seconds>(now - lastWarn).count() << " s ("
        << pendingMin100ns / 1e4 << " to " << pendingMax100ns / 1e4 << " ms, expected "
        << pendingExpectedMs << " ms)";
    SystemEventQueue::push("VID", msg.str());
    pending = 0;
    lastWarn = now;
  }
};
