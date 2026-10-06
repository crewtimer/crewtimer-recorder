
#include "VideoReader.hpp"
#include <Processing.NDI.Lib.h>
#include <algorithm> // For std::min and std::max
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
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

static int64_t systemNow100ns()
{
  return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count() * 10;
}

/**
 * Detects a clock being stepped (e.g. by NTP) from (camera timestamp - PC arrival time).
 * Network delay only lowers that value, so the largest value per 1 s window is the
 * least-delayed frame. A shift of more than 15 ms that then holds steady for 3 windows
 * is reported as a step; slower drift just moves the baseline.
 */
class ClockStepDetector
{
  static constexpr int64_t window100ns = 10000000;       // 1 s
  static constexpr int64_t stepThreshold100ns = 150000;   // 15 ms
  static constexpr int64_t settleTolerance100ns = 50000;  // 5 ms
  static constexpr int confirmWindows = 3;

  int64_t windowStart = 0;
  int64_t windowBest = INT64_MIN;
  bool haveBaseline = false;
  int64_t baseline = 0;
  int64_t baselinePc = 0;
  int64_t candidate = 0;
  int64_t candidatePc = 0;
  int candidateCount = 0;

  // The PC's wall clock minus its monotonic clock only changes when the PC clock is stepped
  static int64_t pcWallMinusMonotonic100ns()
  {
    return systemNow100ns() - duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() * 10;
  }

  static std::string formatMs(int64_t value100ns)
  {
    std::ostringstream os;
    os << (value100ns >= 0 ? "+" : "") << value100ns / 10000 << " ms";
    return os.str();
  }

public:
  /** Returns a warning when a step has just been confirmed, otherwise an empty string. */
  std::string addFrame(int64_t cameraTs100ns, int64_t arrival100ns)
  {
    if (windowStart == 0)
    {
      windowStart = arrival100ns;
    }
    windowBest = std::max(windowBest, cameraTs100ns - arrival100ns);
    if (arrival100ns - windowStart < window100ns)
    {
      return "";
    }
    const int64_t best = windowBest;
    const int64_t pc = pcWallMinusMonotonic100ns();
    windowStart = arrival100ns;
    windowBest = INT64_MIN;

    if (!haveBaseline || std::llabs(best - baseline) < stepThreshold100ns)
    {
      haveBaseline = true;
      baseline = best;
      baselinePc = pc;
      candidateCount = 0;
      return "";
    }
    if (candidateCount == 0 || std::llabs(best - candidate) > settleTolerance100ns)
    {
      candidate = best;
      candidatePc = pc;
      candidateCount = 1;
      return "";
    }
    if (++candidateCount < confirmWindows)
    {
      return "";
    }

    // A PC clock step moves (camera - PC) the opposite way to the PC's own wall clock
    const int64_t pcStep = candidatePc - baselinePc;
    const bool pcStepped = std::llabs(pcStep) >= stepThreshold100ns / 2;
    std::ostringstream message;
    message << "Warning: " << (pcStepped ? "PC" : "Camera") << " clock stepped "
            << formatMs(pcStepped ? pcStep : candidate - baseline)
            << " (camera vs PC offset " << formatMs(baseline) << " -> "
            << formatMs(candidate) << ")";
    baseline = candidate;
    baselinePc = candidatePc;
    candidateCount = 0;
    return message.str();
  }
};

class NdiReader : public VideoReader
{
  /** A wrapper for pNDI_recv to control the lifetime.
   * It needs to stay alive until all frames associated with it have been destroyed.
   */
  class NdiRecv
  {
  public:
    NDIlib_recv_instance_t pNDI_recv;
    NdiRecv(NDIlib_recv_instance_t pNDI_recv) : pNDI_recv(pNDI_recv) {};
    ~NdiRecv()
    {
      NDIlib_recv_connect(pNDI_recv, nullptr);
      NDIlib_recv_destroy(pNDI_recv);
    }
  };

  class NdiFrame : public Frame
  {
    std::shared_ptr<NdiRecv> ndiRecv;
    NDIlib_video_frame_v2_t ndiFrame;

  public:
    NdiFrame(std::shared_ptr<NdiRecv> ndiRecv, NDIlib_video_frame_v2_t ndiFrame)
        : ndiRecv(ndiRecv), ndiFrame(ndiFrame) {

          };
    virtual ~NdiFrame() override
    {
      if (ndiRecv)
      {
        NDIlib_recv_free_video_v2(ndiRecv->pNDI_recv, &ndiFrame);
        ndiRecv = nullptr;
      }
    }
  };

  std::shared_ptr<NdiRecv> ndiRecv;
  std::thread ndiThread;
  std::atomic<bool> keepRunning;
  std::atomic<bool> scanEnabled;
  std::atomic<bool> scanPaused;
  AddFrameFunction addFrameFunction;
  NDIlib_find_instance_t pNDI_find = nullptr;
  std::string srcName;

  std::vector<CameraInfo> camList;
  std::thread ndiScanThread;
  std::mutex scanMutex;

  std::vector<CameraInfo> getCameraList() override
  {
    std::unique_lock<std::mutex> lock(scanMutex);
    return camList;
  }

  /**
   * @brief Search for NDI Sources.  This should always be called from the same
   * thread or defuct threads may cause issues when the program terminates.
   *
   * @return std::vector<CameraInfo>
   */
  std::vector<CameraInfo> findCameras()
  {
    std::vector<CameraInfo> list;
    // Create a finder
    if (pNDI_find == nullptr)
    {
      pNDI_find = NDIlib_find_create_v2();
    }

    if (!pNDI_find)
      return list;

    const NDIlib_source_t *p_sources = NULL;
    uint32_t no_sources = 0;
    // Wait until the sources on the network have changed
    NDIlib_find_wait_for_sources(pNDI_find, 2000);
    p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

    for (uint32_t src = 0; src < no_sources; src++)
    {
      std::string address_port = std::string(p_sources[src].p_url_address);
      std::string ip_address;
      uint16_t port = 0;
      size_t colon_pos = address_port.find(':');
      if (colon_pos != std::string::npos)
      {
        ip_address = address_port.substr(0, colon_pos);
        std::string port_str = address_port.substr(colon_pos + 1);
        try
        {
          port = static_cast<uint16_t>(std::stoi(port_str));
        }
        catch (const std::exception &)
        {
          port = 0; // fallback if conversion fails
        }
      }
      else
      {
        ip_address = address_port;
        port = 0;
      }

      list.push_back(
          CameraInfo(p_sources[src].p_ndi_name, ip_address, port));
      // SystemEventQueue::push("NDI", std::string("Source Found: ") +
      //                                   p_sources[src].p_ndi_name + " at " + p_sources[src].p_ip_address);
    }

    return list;
  };

  void ndiScanLoop()
  {
    std::cout << "NDI Scan loop started" << std::endl;
    while (scanEnabled)
    {
      if (!scanPaused)
      {
        auto list = findCameras();
        {
          std::unique_lock<std::mutex> lock(scanMutex);
          camList = list;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
    if (pNDI_find != nullptr)
    {
      NDIlib_find_destroy(pNDI_find);
      pNDI_find = nullptr;
    }
    std::cout << "NDI Scan loop stopped" << std::endl;
  }

  std::string connect()
  {
    SystemEventQueue::push("Debug", "Searching for NDI sources...");
    NDIlib_source_t p_source;
    CameraInfo foundCamera;
    std::vector<CameraInfo> cameras;
    {
      std::unique_lock<std::mutex> lock(scanMutex);
      for (auto camera : camList)
      {
        std::cout << "Found camera: " << camera.name << " looking for " << srcName << std::endl;
        if (camera.name.find(srcName) == 0)
        {
          foundCamera = camera;
          break;
        }
      }
    }

    if (foundCamera.name == "")
    {
      SystemEventQueue::push("NDI", "Error: Camera not found " + srcName);
      return "";
    }

    if (!ndiRecv)
    {
      // Only create this once as calling destroy on it seems to segfault
      NDIlib_recv_create_v3_t recv_create;
      recv_create.color_format = NDIlib_recv_color_format_UYVY_BGRA;
      auto pNDI_recv = NDIlib_recv_create_v3(&recv_create);
      if (!pNDI_recv)
        return "NDIlib_recv_create_v3() failed";
      ndiRecv = std::make_shared<NdiRecv>(pNDI_recv);
    }
    SystemEventQueue::push("NDI", "Connecting to " + foundCamera.name);
    // Connect to our sources
    p_source.p_ndi_name = foundCamera.name.c_str();
    p_source.p_url_address = foundCamera.url.c_str();

    NDIlib_recv_connect(ndiRecv->pNDI_recv, &p_source);

    foundCamera.name = "";
    cameras.clear();

    return "";
  }

  void run()
  {
    int64_t lastTS = 0;
    int64_t lastArrival100ns = 0;
    int64_t frameCount = 0;
    ClockStepDetector clockSteps;
    connect();
    while (keepRunning)
    {
      NDIlib_video_frame_v2_t video_frame;
      NDIlib_audio_frame_v3_t audio_frame;
      if (!ndiRecv)
      {
        connect();
        if (!ndiRecv)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
          continue;
        }
      }

      auto frameType = NDIlib_recv_capture_v3(ndiRecv->pNDI_recv, &video_frame, nullptr,
                                              nullptr, 5000);
      switch (frameType)
      {
      case NDIlib_frame_type_status_change:
        break;
      // No data
      case NDIlib_frame_type_none:
        SystemEventQueue::push("NDI", "Disconnected: " + srcName);
        ndiRecv = nullptr;
        clockSteps = ClockStepDetector();
        break;

        // Video data
      case NDIlib_frame_type_video:
      {
        if (video_frame.xres && video_frame.yres)
        {
          frameCount++;
          if (frameCount == 1)
          {
            SystemEventQueue::push("NDI", "Connected: " + srcName);
            break; // 1st frame often old frame cached from ndi sender.
                   // Ignore.
          }
          if (video_frame.timestamp == NDIlib_recv_timestamp_undefined)
          {
            std::cerr << "timestamp not supported" << std::endl;
          }
          // std::cout << "Video data received (" << video_frame.xres << "x"
          //           << video_frame.yres << ")" << std::endl;

          auto ts100ns = video_frame.timestamp;
          const auto milli = (5000 + ts100ns) / 10000;

          // Convert utc milliseconds to time_point of system clock
          auto tp =
              std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(milli));

          // Convert to system time_t for conversion to tm structure
          std::time_t raw_time = std::chrono::system_clock::to_time_t(tp);

          // Convert to local time
          std::tm *local_time = std::localtime(&raw_time);
          auto deltaMs = (video_frame.timestamp - lastTS) / 10000;

          auto msPerFrame =
              1000 * video_frame.frame_rate_D / video_frame.frame_rate_N;

          if (frameCount < (2000 / msPerFrame))
          {
            // Ignore the first two seconds as there are often missing or badly timestamped frames
            break;
          }
          const int64_t now100ns = systemNow100ns();
          const int64_t arrivalDeltaMs = (now100ns - lastArrival100ns) / 10000;
          if (deltaMs == 0 || (lastTS != 0 && deltaMs >= 2 * msPerFrame))
          {
            std::stringstream timestring;
            timestring << std::put_time(local_time, "%H:%M:%S") << "." << std::setw(3)
                       << std::setfill('0') << milli % 1000;

            // For diagnostic purposes
            std::stringstream message;
            if (deltaMs == 0)
            {
              message << "Duplicate frame timestamp at " << timestring.str();
            }
            else
            {
              int framesMissing = std::round(double(deltaMs) / msPerFrame - 1);
              message << "Gap=" << deltaMs << "ms (" << framesMissing << " frames missing) prior to " << timestring.str();
              if (lastArrival100ns != 0 && arrivalDeltaMs < deltaMs / 2)
              {
                message << " - frames arrived " << arrivalDeltaMs
                        << "ms apart: likely a clock step, not lost frames";
              }
            }
            std::cerr << message.str() << std::endl;

            if ((lastTS != 0 && deltaMs >= 110) || reportAllGaps)
            {
              const auto errmsg = std::string("Error: ") + message.str();
              SystemEventQueue::push("NDI", errmsg);
            }
          }

          const auto step = clockSteps.addFrame(video_frame.timestamp, now100ns);
          if (!step.empty())
          {
            std::cerr << step << std::endl;
            SystemEventQueue::push("NDI", step);
          }

          lastTS = video_frame.timestamp;
          lastArrival100ns = now100ns;
          auto txframe = std::make_shared<NdiFrame>(ndiRecv, video_frame);
          txframe->xres = video_frame.xres & ~1; // force even
          txframe->yres = video_frame.yres & ~1;
          txframe->stride = video_frame.line_stride_in_bytes;
          txframe->timestamp = video_frame.timestamp;
          txframe->receivedTs100ns = now100ns;
          txframe->data = video_frame.p_data;
          txframe->frame_rate_N = video_frame.frame_rate_N;
          txframe->frame_rate_D = video_frame.frame_rate_D;
          txframe->pixelFormat = Frame::PixelFormat::UYVY422;
          if (addFrameFunction)
          {
            addFrameFunction(txframe);
          }
        }
        else
        {
          NDIlib_recv_free_video_v2(ndiRecv->pNDI_recv, &video_frame);
        }
      }
      break;

        // Audio data
      case NDIlib_frame_type_audio:
      {
        // printf("Audio data received (%d samples @%d fs).\n",
        // audio_frame.no_samples, audio_frame.sample_rate);
        // auto level = detectTone((int *)audio_frame.p_data,
        // audio_frame.no_samples,
        //                         audio_frame.sample_rate, 400);

        NDIlib_recv_free_audio_v3(ndiRecv->pNDI_recv, &audio_frame);
      }
      break;
      default:
        break;
      }
    }
  }

public:
  NdiReader()
  {
    scanEnabled = true;

    ndiScanThread = std::thread(&NdiReader::ndiScanLoop, this);
    auto *version = NDIlib_version();
    std::cout << "NDI SDK Version: " << version << std::endl;
  }
  std::string start(const CameraInfo &camera,
                    AddFrameFunction addFrameFunction) override
  {
    scanPaused = true;
    if (ndiThread.joinable())
    {
      stop();
    }
    // Pre-seed camList so connect() can find the source without waiting for the
    // scan loop, which is paused during active use.
    {
      std::unique_lock<std::mutex> lock(scanMutex);
      camList = {camera};
    }
    this->srcName = camera.name;
    this->addFrameFunction = addFrameFunction;
    keepRunning = true;
    ndiThread = std::thread(&NdiReader::run, this);
    return "";
  };
  std::string stop() override
  {
    scanPaused = false;
    keepRunning = false;
    if (ndiThread.joinable())
    {
      ndiThread.join();
    }
    if (addFrameFunction)
    {
      addFrameFunction = nullptr;
    }

    ndiRecv = nullptr;
    return "";
  }
  virtual ~NdiReader() override
  {
    std::cerr << "Destroy NdiReader Start" << std::endl;
    stop();
    scanPaused = true;
    scanEnabled = false;

    if (ndiScanThread.joinable())
    {
      ndiScanThread.join();
    }

    // Not required, but nice
    NDIlib_destroy();
    std::cerr << "Destroy NdiReader Finish" << std::endl;
  };
};

std::shared_ptr<VideoReader> createNdiReader()
{
  return std::shared_ptr<NdiReader>(new NdiReader());
}
