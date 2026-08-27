#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "FrameProcessor.hpp"
#include "Message.hpp"
#include "MulticastReceiver.hpp"
#include "SystemEventQueue.hpp"
#include "VideoReader.hpp"
#include "VideoRecorder.hpp"

#include "mdns/ndi_mdns.hpp"

class VideoController
{
public:
  struct StatusInfo
  {
    bool recording = false;
    std::string error;
    std::uint32_t recordingDuration = 0;
    FrameProcessor::StatusInfo frameProcessor;

    friend std::ostream &operator<<(std::ostream &os, const StatusInfo &info)
    {
      os << "{recording=" << info.recording
         << ", error='" << info.error << "'"
         << ", duration=" << info.recordingDuration
         << ", frameProcessor=" << info.frameProcessor << "}";
      return os;
    }
  };

private:
  std::string activeProtocol;
  std::string srcName;
  std::string encoder;
  std::string dir;
  std::string prefix;
  std::string waypoint;
  int interval;
  std::thread monitorThread;
  std::atomic<bool> monitorStopRequested;

  std::chrono::steady_clock::time_point startTime;
  std::string status;
  std::recursive_mutex
      controlMutex; ///< Mutex for synchronizing access to start/stop actions.
  std::shared_ptr<VideoRecorder> videoRecorder;
  std::shared_ptr<FrameProcessor> frameProcessor;
  std::shared_ptr<VideoReader> videoReader;
  std::shared_ptr<MulticastReceiver> mcastListener;
  StatusInfo statusInfo;
  bool previewRequested = false;
  bool readerRunning = false;
  std::string activeSourceName;
  FramePtr lastPreviewFrame;
  std::mutex frameSinkMutex;
  std::atomic<int> frameRotation{0};

  // mdns
  std::shared_ptr<ndi_mdns::NdiMdns> mdns;
  std::thread mdnsScanThread;
  std::vector<VideoReader::CameraInfo> camList;
  std::mutex scanMutex;
  std::atomic<bool> scanEnabled;
  std::atomic<bool> scanPaused;

  bool findCamera(const std::string &cameraName, VideoReader::CameraInfo &camera)
  {
    std::unique_lock<std::mutex> lock(scanMutex);
    for (auto &cam : camList)
    {
      if (cam.name == cameraName)
      {
        camera = cam;
        return true;
      }
    }
    return false;
  }

  std::string ensureVideoReader(const std::string &protocol)
  {
    if (activeProtocol == protocol && videoReader)
    {
      return "";
    }

    if (readerRunning && videoReader)
    {
      videoReader->stop();
      readerRunning = false;
    }

    activeProtocol = protocol;
    videoReader = nullptr;

    if (protocol == "BASLER")
    {
#ifdef HAVE_BASLER
      videoReader = createBaslerReader();
#endif
      if (!videoReader)
      {
        return "Basler support not compiled in.";
      }
    }
    else if (protocol == "SRT")
    {
      videoReader = createSrtReader();
    }
    else if (protocol == "NDI")
    {
      videoReader = createNdiReader();
    }
    else
    {
      videoReader = createSrtReader();
    }

    return "";
  }

  std::string startReader(const VideoReader::CameraInfo &camera,
                          const std::string &protocol,
                          const bool reportAllGaps)
  {
    if (readerRunning && activeProtocol == protocol && activeSourceName == camera.name)
    {
      if (videoReader)
      {
        videoReader->setProperties(reportAllGaps);
      }
      return "";
    }

    if (readerRunning && videoReader)
    {
      videoReader->stop();
      readerRunning = false;
    }

    auto err = ensureVideoReader(protocol);
    if (!err.empty())
    {
      return err;
    }
    if (!videoReader)
    {
      return "Error: Unable to create video reader.";
    }

    {
      std::lock_guard<std::mutex> lock(frameSinkMutex);
      lastPreviewFrame = nullptr;
    }

    videoReader->setProperties(reportAllGaps);
    err = videoReader->start(camera, [this](FramePtr frame)
                             {
                               if (frame->frameType == Frame::FrameType::VIDEO &&
                                   frameRotation.load() != 0)
                               {
                                 auto rotated = rotateFrame90(frame, frameRotation.load() == 90);
                                 if (rotated)
                                   frame = rotated;
                               }
                               std::shared_ptr<FrameProcessor> processor;
                               {
                                 std::lock_guard<std::mutex> lock(frameSinkMutex);
                                 if (frame->frameType == Frame::FrameType::VIDEO)
                                 {
                                   lastPreviewFrame = frame;
                                 }
                                 processor = frameProcessor;
                               }
                               if (processor)
                               {
                                 processor->addFrame(frame);
                               } });
    if (!err.empty())
    {
      return err;
    }

    readerRunning = true;
    activeSourceName = camera.name;
    scanPaused = true;
    return "";
  }

  void stopReader()
  {
    if (readerRunning && videoReader)
    {
      videoReader->stop();
    }
    readerRunning = false;
    activeSourceName.clear();
    scanPaused = false;
    {
      std::lock_guard<std::mutex> lock(frameSinkMutex);
      lastPreviewFrame = nullptr;
    }
  }

  void mdnsScanLoop()
  {
    std::cout << "mDns Scan loop started" << std::endl;
    while (scanEnabled)
    {
      if (!scanPaused)
      {
        auto ndilist = mdns->discover();
        std::vector<VideoReader::CameraInfo> list;
        for (auto &s : ndilist)
        {
          if (!s.ipv4.empty())
          {
            list.push_back(
                VideoReader::CameraInfo(s.instance_label, s.ipv4[0], s.port));
          }
          std::cout << s.instance << " -> " << s.host << ":" << s.port << "\n";
          std::cout << "NDI Source: " << s.instance_label << " [" << s.service << "." << s.domain << "] -> "
                    << s.host << ":" << s.port << std::endl;
          for (auto &ip : s.ipv4)
            std::cout << "  A    " << ip << "\n";
          for (auto &ip : s.ipv6)
            std::cout << "  AAAA " << ip << "\n";
          for (auto &kv : s.txt)
            std::cout << "  TXT  " << kv << "\n";
        }

        {
          std::unique_lock<std::mutex> lock(scanMutex);
          camList = list;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }

    std::cout << "mDns Scan loop stopped" << std::endl;
  }

public:
  VideoController()
  {
    scanEnabled = true;

    monitorStopRequested = false;
    monitorThread = std::thread(&VideoController::monitorLoop, this);

    mcastListener = std::shared_ptr<MulticastReceiver>(
        new MulticastReceiver("239.215.23.42", 52342));
    mcastListener->setMessageCallback([this](const json &j)
                                      {
                                        // std::cerr << "Received JSON: " << j.dump() << std::endl;
                                        auto command = j.value<std::string>("cmd", "");
                                        auto waypoint = j.value<std::string>("wp","");
                                        // std::cerr << "cmd=" << command << " wp=" << waypoint << " this->wp='" << this->waypoint << "'" << std::endl;
                                        if (command == "split-video" && this->frameProcessor && (this->waypoint.empty() || this->waypoint == waypoint))
                                        {
                                          std::cerr << "Splitting Video" << std::endl;
                                          this->frameProcessor->splitFile();
                                        }
                                        // Allow renderer access to mcast messages
                                        sendMessageToRenderer("mcast", std::make_shared<json>(j)); });

    auto retval = mcastListener->start();
    if (!retval.empty())
    {
      SystemEventQueue::push("VID", "Error: Unable to create mcast listener.");
    }

    ndi_mdns::DiscoverOptions opt;
    opt.timeout = std::chrono::seconds(2);
    opt.debug = false;
    opt.debug_level = 2;
    opt.reenumerate_interval_ms = 5000; // 0 = disabled; e.g. 2000 to re-enumerate every 2s
    mdns = std::make_shared<ndi_mdns::NdiMdns>(opt);
    mdnsScanThread = std::thread(&VideoController::mdnsScanLoop, this);
  }
  ~VideoController()
  {
    SystemEventQueue::push("VID", "Stopping multicast listener...");
    mcastListener->stop();
    mcastListener = nullptr;

    monitorStopRequested = true;

    stop();
    stopPreview();
    scanEnabled = false;
    scanPaused = true;

    videoReader = nullptr;
    if (monitorThread.joinable())
    {
      monitorThread.join();
    }

    if (mdnsScanThread.joinable())
    {
      mdnsScanThread.join();
    }

    mdns = nullptr;
  }

  void setWaypoint(std::string &waypoint)
  {
    this->waypoint = waypoint;
  }

  std::vector<VideoReader::CameraInfo> getCameraList()
  {
    // std::lock_guard<std::recursive_mutex> lock(controlMutex);
    // if (videoReader)
    // {
    //   return videoReader->getCameraList();
    // }
    std::unique_lock<std::mutex> lock(scanMutex);
    return camList;
  }

  StatusInfo getStatus()
  {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);

    // compute recording and recordingDuration status fields
    uint32_t elapsedSecondsUInt = 0;
    std::shared_ptr<FrameProcessor> processor;
    {
      std::lock_guard<std::mutex> lock(frameSinkMutex);
      processor = frameProcessor;
    }
    const auto recording = processor != nullptr && status == "";
    if (recording)
    {
      std::chrono::steady_clock::time_point endTime =
          std::chrono::steady_clock::now();
      std::chrono::duration<double> elapsed_seconds = endTime - startTime;
      elapsedSecondsUInt = static_cast<uint32_t>(elapsed_seconds.count());
    }
    statusInfo.recording = recording;
    statusInfo.recordingDuration = recording ? elapsedSecondsUInt : 0;
    return statusInfo;
  }

  std::string start(const std::string srcName, const std::string protocol,
                    const std::string encoder,
                    const std::string dir, const std::string prefix,
                    const int interval,
                    const FrameProcessor::FRectangle cropArea,
                    const FrameProcessor::Guide guide,
                    const int rotation,
                    const bool reportAllGaps)
  {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    this->srcName = srcName;
    this->encoder = encoder;
    this->dir = dir;
    this->prefix = prefix;
    this->interval = interval;
    frameRotation = rotation;
    statusInfo.error = "";
    statusInfo.frameProcessor.error = "";
    if (videoRecorder)
    {
      return "Video Controller already running";
    }

    VideoReader::CameraInfo camera;
    if (!findCamera(srcName, camera))
    {
      return "Camera source not found: " + srcName;
    }

    startTime = std::chrono::steady_clock::now();
    status = "";
    std::string retval = "";
#ifdef USE_APPLE
    if (encoder == "apple")
    {
      SystemEventQueue::push("VID", "Using Apple VideoToolbox encoder.");
      videoRecorder = createAppleRecorder();
    }
#endif

#ifdef USE_OPENCV
    if (encoder == "opencv")
    {
      SystemEventQueue::push("VID", "Using opencv encoder.");
      videoRecorder = createOpenCVRecorder();
      // default
    }
#endif

    if (encoder == "ffmpeg")
    {
      SystemEventQueue::push("VID", "Using ffmpeg encoder.");
      videoRecorder = createFfmpegRecorder();
    }

    if (encoder == "null")
    {
      SystemEventQueue::push("VID", "Using null encoder.");
      videoRecorder = createNullRecorder();
    }

    if (!videoRecorder)
    {
      auto msg = "Error: Unknown encoder type: " + encoder;
      SystemEventQueue::push("VID", msg);
      return msg;
    }

    auto processor = std::shared_ptr<FrameProcessor>(new FrameProcessor(
        dir, prefix, videoRecorder, interval, cropArea, guide));
    {
      std::lock_guard<std::mutex> lock(frameSinkMutex);
      frameProcessor = processor;
    }

    retval = startReader(camera, protocol, reportAllGaps);
    if (!retval.empty())
    {
      {
        std::lock_guard<std::mutex> lock(frameSinkMutex);
        frameProcessor = nullptr;
      }
      processor->stop();
      videoRecorder->stop();
      videoRecorder = nullptr;
      return retval;
    }

    auto fpStatus = processor->getStatus();
    if (!fpStatus.recording)
    {
      {
        std::lock_guard<std::mutex> lock(frameSinkMutex);
        frameProcessor = nullptr;
      }
      processor->stop();
      videoRecorder->stop();
      videoRecorder = nullptr;
      if (!previewRequested)
      {
        stopReader();
      }
      return fpStatus.error;
    }

    return "";
  }

  std::string stop()
  {
    statusInfo.recording = false;
    std::shared_ptr<FrameProcessor> processor;
    {
      std::lock_guard<std::mutex> lock(frameSinkMutex);
      processor = frameProcessor;
      frameProcessor = nullptr;
    }
    if (!processor)
    {
      return "";
    }
    SystemEventQueue::push("VID", "Shutting down video controller...");

    SystemEventQueue::push("VID", "Stopping frame processor...");
    processor->stop();

    SystemEventQueue::push("VID", "Stopping recorder...");
    videoRecorder->stop();
    videoRecorder = nullptr;

    if (!previewRequested)
    {
      SystemEventQueue::push("VID", "Stopping video reader...");
      stopReader();
    }

    SystemEventQueue::push("VID", "VideoController stopped");
    return "";
  }

  std::string startPreview(const std::string srcName, const std::string protocol,
                           const int rotation)
  {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    frameRotation = rotation;
    VideoReader::CameraInfo camera;
    if (!findCamera(srcName, camera))
    {
      return "Camera source not found: " + srcName;
    }

    previewRequested = true;
    return startReader(camera, protocol, false);
  }

  std::string stopPreview()
  {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    previewRequested = false;
    if (!videoRecorder)
    {
      stopReader();
    }
    return "";
  }

  FramePtr getLastFrame()
  {
    std::lock_guard<std::mutex> lock(frameSinkMutex);
    if (lastPreviewFrame)
    {
      return lastPreviewFrame;
    }
    return nullptr;
  }
  void monitorLoop()
  {
    while (!monitorStopRequested)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      std::lock_guard<std::recursive_mutex> lock(controlMutex);
      std::shared_ptr<FrameProcessor> processor;
      {
        std::lock_guard<std::mutex> sinkLock(frameSinkMutex);
        processor = frameProcessor;
      }
      if (videoRecorder && processor)
      {
        statusInfo.frameProcessor = processor->getStatus();
        if (!statusInfo.frameProcessor.error.empty())
        {
          statusInfo.error = statusInfo.frameProcessor.error;
          SystemEventQueue::push("system", statusInfo.error);
          stop();
        }
      }
    }
  }
};
