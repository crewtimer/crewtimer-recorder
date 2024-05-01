#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "FrameProcessor.hpp"
#include "MulticastReceiver.hpp"
#include "VideoReader.hpp"
#include "VideoRecorder.hpp"

class VideoController {
public:
  struct StatusInfo {
    bool recording;
    std::string error;
    std::uint32_t recordingDuration;
    FrameProcessor::StatusInfo frameProcessor;
  };

private:
  const std::string srcName;
  const std::string encoder;
  const std::string dir;
  const std::string prefix;
  const int interval;
  std::thread monitorThread;

  std::chrono::steady_clock::time_point startTime;
  std::atomic<bool> monitorStopRequested;
  std::string status;
  std::recursive_mutex
      controlMutex; ///< Mutex for synchronizing access to start/stop actions.
  std::shared_ptr<VideoRecorder> videoRecorder;
  std::shared_ptr<FrameProcessor> frameProcessor;
  std::shared_ptr<VideoReader> videoReader;
#ifndef _WIN32
  std::shared_ptr<MulticastReceiver> mcastListener;
#endif
  StatusInfo statusInfo;

public:
  VideoController(const std::string srcName, const std::string encoder,
                  const std::string dir, const std::string prefix,
                  const int interval)
      : srcName(srcName), encoder(encoder), dir(dir), prefix(prefix),
        interval(interval), monitorThread(&VideoController::monitorLoop, this) {
  }
  ~VideoController() {
    monitorStopRequested = true;
    stop();
    if (monitorThread.joinable()) {
      monitorThread.join();
    }
  }
  StatusInfo getStatus() {
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

  std::string start() {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (videoRecorder) {
      return "Video Controller already running";
    }
    status = "";
    std::string retval = "";
#ifdef USE_APPLE
    if (encoder == "apple") {
      std::cout << "Using Apple VideoToolbox api." << std::endl;
      videoRecorder = createAppleRecorder();
    }
#endif

#ifdef USE_OPENCV
    if (encoder == "opencv") {
      std::cout << "Using opencv api." << std::endl;
      videoRecorder = createOpenCVRecorder();
      // default
    }
#endif

    if (encoder == "ffmpeg") {
      std::cout << "Using ffmpeg api." << std::endl;
      videoRecorder = createFfmpegRecorder();
    }

    if (!videoRecorder) {
      auto msg = "Unknown encoder type: " + encoder;
      std::cerr << msg << std::endl;
      return msg;
    }

    frameProcessor = std::shared_ptr<FrameProcessor>(
        new FrameProcessor(dir, prefix, videoRecorder, interval));

    // auto reader = createBaslerReader();
    videoReader = createNdiReader(srcName);
    retval = videoReader->open(frameProcessor);
    if (!retval.empty()) {
      return retval;
    }
    retval = videoReader->start();
    if (!retval.empty()) {
      return retval;
    }

    auto fpStatus = frameProcessor->getStatus();
    if (!fpStatus.recording) {
      return fpStatus.error;
    }

#ifndef _WIN32
    mcastListener = std::shared_ptr<MulticastReceiver>(
        new MulticastReceiver("239.215.23.42", 52342));
    mcastListener->setMessageCallback([this](const json &j) {
      std::cout << "Received JSON: " << j.dump() << std::endl;
      auto command = j.value<std::string>("cmd", "");
      if (command == "split-video") {
        this->frameProcessor->splitFile();
      }
    });

    retval = mcastListener->start();
    if (!retval.empty()) {
      return retval;
    }
#endif
    startTime = std::chrono::steady_clock::now();
    return "";
  }

  std::string stop() {
    std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (!frameProcessor) {
      return "";
    }
    std::cout << "Shutting down..." << std::endl;

    std::cout << "Stopping multicast listener..." << std::endl;
#ifndef _WIN32
    mcastListener->stop();
    mcastListener = nullptr;
#endif
    std::cout << "Stopping video reader..." << std::endl;
    videoReader->stop();
    videoReader = nullptr;

    std::cout << "Stopping frame processor..." << std::endl;
    frameProcessor->stop();
    frameProcessor = nullptr;

    std::cout << "Stopping recorder..." << std::endl;
    videoRecorder->stop();
    videoRecorder = nullptr;

    std::cout << "Recording finished" << std::endl;
    return "";
  }

  void monitorLoop() {
    while (!monitorStopRequested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      std::lock_guard<std::recursive_mutex> lock(controlMutex);
      if (videoRecorder && frameProcessor) {
        statusInfo.frameProcessor = frameProcessor->getStatus();
        if (!statusInfo.frameProcessor.error.empty()) {
          statusInfo.error = statusInfo.frameProcessor.error;
          stop();
        }
      }
    }
  }
};
