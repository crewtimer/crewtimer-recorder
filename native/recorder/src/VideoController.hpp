#include <string>
#include <thread>
#include <vector>

#include "FrameProcessor.hpp"
#include "MulticastReceiver.hpp"
#include "VideoReader.hpp"
#include "VideoRecorder.hpp"

class VideoController {
  const std::string srcName;
  const std::string encoder;
  const std::string dir;
  const std::string prefix;
  const int interval;
  std::shared_ptr<VideoRecorder> videoRecorder;
  std::shared_ptr<FrameProcessor> frameProcessor;
  std::shared_ptr<MulticastReceiver> mcastListener;
  std::shared_ptr<VideoReader> videoReader;

public:
  VideoController(const std::string srcName, const std::string encoder,
                  const std::string dir, const std::string prefix,
                  const int interval)
      : srcName(srcName), encoder(encoder), dir(dir), prefix(prefix),
        interval(interval) {}
  void start() {
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
      throw std::runtime_error("Unknown encoder type: " + encoder);
    }

    frameProcessor = std::shared_ptr<FrameProcessor>(
        new FrameProcessor(dir, prefix, videoRecorder, interval));

    // auto reader = createBaslerReader();
    videoReader = createNdiReader(srcName);
    videoReader->open(frameProcessor);
    videoReader->start();

    mcastListener = std::shared_ptr<MulticastReceiver>(
        new MulticastReceiver("239.215.23.42", 52342));
    mcastListener->setMessageCallback([this](const json &j) {
      std::cout << "Received JSON: " << j.dump() << std::endl;
      auto command = j.value<std::string>("cmd", "");
      if (command == "split-video") {
        this->frameProcessor->splitFile();
      }
    });

    mcastListener->start();
  }

  void stop() {
    std::cout << "Shutting down..." << std::endl;

    std::cout << "Stopping multicast listener..." << std::endl;
    mcastListener->stop();
    mcastListener = nullptr;

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
  }
};
