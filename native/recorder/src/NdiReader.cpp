
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
#include "mdns/ndi_mdns.hpp"

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif // _WIN32

using namespace std::chrono;

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
  std::thread scanThread;
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
      list.push_back(
          CameraInfo(p_sources[src].p_ndi_name, p_sources[src].p_url_address));
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

  void mdnsScanLoop()
  {
    std::cout << "mDns Scan loop started" << std::endl;
    while (scanEnabled)
    {
      if (!scanPaused)
      {
        ndi_mdns::DiscoverOptions opt;
        opt.timeout = std::chrono::seconds(2);
        opt.debug = false; // set true for [DBG] lines
        opt.debug_level = 2;
        // opt.interface_ipv4 = "10.0.1.5";   // pick your NIC if needed

        auto ndilist = ndi_mdns::discover(opt);
        std::vector<CameraInfo> list;
        for (auto &s : ndilist)
        {
          if (!s.ipv4.empty())
          {
            list.push_back(
                CameraInfo(s.instance_label, s.ipv4[0]));
          }
          // std::cout << s.instance << " -> " << s.host << ":" << s.port << "\n";
          // std::cout << "NDI Source: " << s.instance_label << " [" << s.service << "." << s.domain << "] -> "
          //           << s.host << ":" << s.port << std::endl;
          // for (auto &ip : s.ipv4)
          //   std::cout << "  A    " << ip << "\n";
          // for (auto &ip : s.ipv6)
          //   std::cout << "  AAAA " << ip << "\n";
          // for (auto &kv : s.txt)
          //   std::cout << "  TXT  " << kv << "\n";
        }

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
    std::cout << "mDns Scan loop stopped" << std::endl;
  }

  std::string connect()
  {
    SystemEventQueue::push("Debug", "Searching for NDI sources...");
    NDIlib_source_t p_source;
    CameraInfo foundCamera;
    std::vector<CameraInfo> cameras;
    while (foundCamera.name == "" && keepRunning)
    {
      std::unique_lock<std::mutex> lock(scanMutex);
      cameras = camList;
      for (auto camera : cameras)
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
      return ""; // stop received before ndi source found
    }

    std::cout << "Camera found" << std::endl;
    if (!ndiRecv)
    {
      std::cout << "Connecting..." << std::endl;
      // Only create this once as calling destroy on it seems to segfault
      NDIlib_recv_create_v3_t recv_create;

      recv_create.color_format = NDIlib_recv_color_format_UYVY_BGRA;
      // We now have at least one source, so we create a receiver to look at it.
      auto pNDI_recv = NDIlib_recv_create_v3(&recv_create);
      if (!pNDI_recv)
        return "NDIlib_recv_create_v3() failed";
      ndiRecv = std::make_shared<NdiRecv>(pNDI_recv);
    }
    // Connect to the source
    SystemEventQueue::push("Debug", std::string("Connecting to ") +
                                        foundCamera.name + " at " +
                                        foundCamera.address);
    // Connect to our sources
    p_source.p_ndi_name = foundCamera.name.c_str();
    p_source.p_url_address = foundCamera.address.c_str();
    NDIlib_recv_connect(ndiRecv->pNDI_recv, &p_source);

    foundCamera.name = "";
    cameras.clear();

    return "";
  }

  void run()
  {
    int64_t lastTS = 0;
    int64_t frameCount = 0;
    connect();
    while (keepRunning)
    {
      NDIlib_video_frame_v2_t video_frame;
      NDIlib_audio_frame_v3_t audio_frame;
      if (!ndiRecv)
      {
        connect();
      }

      auto frameType = NDIlib_recv_capture_v3(ndiRecv->pNDI_recv, &video_frame, nullptr,
                                              nullptr, 5000);
      switch (frameType)
      {
      case NDIlib_frame_type_status_change:
        break;
      // No data
      case NDIlib_frame_type_none:
        SystemEventQueue::push("NDI", std::string("Error: No data received"));
        ndiRecv = nullptr;
        break;

        // Video data
      case NDIlib_frame_type_video:
      {
        if (video_frame.xres && video_frame.yres)
        {
          frameCount++;
          if (frameCount == 1)
          {
            SystemEventQueue::push("Debug", std::string("Stream active"));
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
          time_point<system_clock> tp =
              time_point<system_clock>(milliseconds(milli));

          // Convert to system time_t for conversion to tm structure
          std::time_t raw_time = system_clock::to_time_t(tp);

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
              message << "Gap=" << deltaMs << "ms (" << framesMissing << "frames missing) prior to " << timestring.str();
            }
            std::cerr << message.str() << std::endl;

            if ((lastTS != 0 && deltaMs >= 110) || reportAllGaps)
            {
              const auto errmsg = std::string("Error: ") + message.str();
              SystemEventQueue::push("NDI", errmsg);
            }
          }

          lastTS = video_frame.timestamp;
          auto txframe = std::make_shared<NdiFrame>(ndiRecv, video_frame);
          txframe->xres = video_frame.xres & ~1; // force even
          txframe->yres = video_frame.yres & ~1;
          txframe->stride = video_frame.line_stride_in_bytes;
          txframe->timestamp = video_frame.timestamp;
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
    scanThread = std::thread(&NdiReader::mdnsScanLoop, this);
    auto *version = NDIlib_version();
    std::cout << "NDI SDK Version: " << version << std::endl;
  }
  std::string start(const std::string srcName,
                    AddFrameFunction addFrameFunction) override
  {
    scanPaused = true;
    if (ndiThread.joinable())
    {
      stop();
    }
    this->srcName = srcName;
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
    stop();
    scanPaused = true;
    scanEnabled = false;
    if (scanThread.joinable())
    {
      scanThread.join();
    }

    // Not required, but nice
    NDIlib_destroy();
  };
};

std::shared_ptr<VideoReader> createNdiReader()
{
  return std::shared_ptr<NdiReader>(new NdiReader());
}
