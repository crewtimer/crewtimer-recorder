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

class VideoController
{
public:
  struct StatusInfo
  {
    bool recording;
    std::string error;
    std::uint32_t recordingDuration;
    FrameProcessor::StatusInfo frameProcessor;
  };

private:
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

public:
  VideoController(const std::string _camType)
  {

#ifdef HAVE_BASLER
    if (camType == "basler")
    { // basler camera
      videoReader = createBaslerReader();
    }
    else
    {
      videoReader = createNdiReader();
    }
#else
    videoReader = createNdiReader();
#endif

    monitorStopRequested = false;
    monitorThread = std::thread(&VideoController::monitorLoop, this);

    mcastListener = std::shared_ptr<MulticastReceiver>(
        new MulticastReceiver("239.215.23.42", 52342));
    mcastListener->setMessageCallback([this](const json &j)
                                      {
                                        // std::cerr << "Received JSON: " << j.dump() << std::endl;
                                        auto command = j.value<std::string>("cmd", "");
                                        auto waypoint = j.value<std::string>("wp","");
                                        if (command == "split-video" && this->frameProcessor && (this->waypoint.empty() || this->waypoint == waypoint))
                                        {
                                          this->frameProcessor->splitFile();
                                        }
                                        // Allow renderer access to mcast messages
                                        sendMessageToRenderer("mcast", std::make_shared<json>(j)); });

    auto retval = mcastListener->start();
    if (!retval.empty())
    {
      SystemEventQueue::push("VID", "Error: Unable to create mcast listener.");
    }
  }
  ~VideoController()
  {
    SystemEventQueue::push("VID", "Stopping multicast listener...");
    mcastListener->stop();
    mcastListener = nullptr;

    monitorStopRequested = true;
    stop();
    videoReader = nullptr;
    if (monitorThread.joinable())
    {
      monitorThread.join();
    }
  }

  void setWaypoint(std::string &waypoint)
  {
    this->waypoint = waypoint;
  }

  std::vector<VideoReader::CameraInfo> getCameraList()
  {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (videoReader)
    {
      return videoReader->getCameraList();
    }
    return std::vector<VideoReader::CameraInfo>();
  }

  StatusInfo getStatus()
  {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    std::chrono::steady_clock::time_point endTime =
        std::chrono::steady_clock::now();

    // Calculate the elapsed time in seconds
    std::chrono::duration<double> elapsed_seconds = endTime - startTime;
    uint32_t elapsedSecondsUInt =
        static_cast<uint32_t>(elapsed_seconds.count());

    const auto recording = frameProcessor != nullptr && status == "";
    statusInfo.recording = recording;
    statusInfo.recordingDuration = recording ? elapsedSecondsUInt : 0;
    return statusInfo;
  }

  std::string start(const std::string srcName, const std::string encoder,
                    const std::string dir, const std::string prefix,
                    const int interval,
                    const FrameProcessor::FRectangle cropArea,
                    const FrameProcessor::Guide guide,
                    const bool reportAllGaps, const bool addTimeOverlay)
  {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    this->srcName = srcName;
    this->encoder = encoder;
    this->dir = dir;
    this->prefix = prefix;
    this->interval = interval;
    statusInfo.error = "";
    statusInfo.frameProcessor.error = "";
    if (videoRecorder)
    {
      return "Video Controller already running";
    }
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

    frameProcessor = std::shared_ptr<FrameProcessor>(new FrameProcessor(
        dir, prefix, videoRecorder, interval, cropArea, guide, addTimeOverlay));

    videoReader->setProperties(reportAllGaps);
    retval = videoReader->start(srcName, [this](FramePtr frame)
                                { this->frameProcessor->addFrame(frame); });
    if (!retval.empty())
    {
      return retval;
    }

    auto fpStatus = frameProcessor->getStatus();
    if (!fpStatus.recording)
    {
      return fpStatus.error;
    }

    startTime = std::chrono::steady_clock::now();
    return "";
  }

  std::string stop()
  {
    statusInfo.recording = false;
    if (!frameProcessor)
    {
      return "";
    }
    SystemEventQueue::push("VID", "Shutting down...");

    SystemEventQueue::push("VID", "Stopping video reader...");
    videoReader->stop();
    // Do not null the reader since we rely on it to query  cameras

    SystemEventQueue::push("VID", "Stopping frame processor...");
    frameProcessor->stop();
    frameProcessor = nullptr;

    SystemEventQueue::push("VID", "Stopping recorder...");
    videoRecorder->stop();
    videoRecorder = nullptr;

    SystemEventQueue::push("VID", "VideoController stopped");
    return "";
  }

  FramePtr getLastFrame()
  {
    if (frameProcessor)
    {
      return frameProcessor->getLastFrame();
    }
    else
    {
      return nullptr;
    }
  }
  void monitorLoop()
  {
    while (!monitorStopRequested)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      std::lock_guard<std::recursive_mutex> lock(controlMutex);
      if (videoRecorder && frameProcessor)
      {
        statusInfo.frameProcessor = frameProcessor->getStatus();
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
