#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include "VideoRecorder.hpp"
#include "VideoUtils.hpp"

/**
 * Class responsible for processing video frames.
 */
class FrameProcessor {

public:
  struct StatusInfo {
    bool recording;
    std::string error;
    std::string filename;
    std::uint32_t width;
    std::uint32_t height;
    float fps;
  };

  struct Rectangle {
    int x;
    int y;
    int width;
    int height;
    Rectangle(int x, int y, int width, int height)
        : x(x), y(y), width(width), height(height) {}
  };

  /**
   * Constructor for FrameProcessor.
   * Initializes and starts capture and process threads.
   */
  FrameProcessor(const std::string directory, const std::string prefix,
                 std::shared_ptr<VideoRecorder> videoRecorder, int durationSecs,
                 Rectangle cropArea);

  void addFrame(FramePtr frame);

  /**
   * Destructor for FrameProcessor.
   * Ensures that capture and process threads are joined properly.
   */
  ~FrameProcessor();

  /**
   * Stops the capture and processing threads.
   */
  void stop();

  /**
   * @brief Triggers the frame processor to close the currently open file and
   * open a new file.
   */
  void splitFile();

  StatusInfo getStatus();

  /**
   * @brief Get the Last Frame object received by the frame processor
   *
   * @return FramePtr
   */
  FramePtr getLastFrame();

private:
  StatusInfo statusInfo;
  std::string errorMessage;
  const std::string directory;
  const std::string prefix;
  Rectangle cropArea;
  std::shared_ptr<VideoRecorder> videoRecorder;
  uint64_t durationSecs;
  uint64_t nextStartTime = 0;
  std::atomic<bool> splitRequested;
  std::queue<FramePtr> frameQueue; ///< Queue to hold the frames.
  std::mutex queueMutex; ///< Mutex for synchronizing access to the frame queue.
  std::condition_variable frameAvailable; ///< Condition variable to notify
                                          ///< when frames are available.
  std::atomic<bool>
      running; ///< Atomic flag to control the running state of threads.
  std::thread processThread; ///< Threads for capturing and processing frames.
  FramePtr lastFrame;

  // JSON file props
  int64_t frameCount = 0;
  uint64_t startTs;
  uint64_t lastTs;
  int lastXres = 0;
  int lastYres = 0;
  float lastFPS = 0;
  std::string jsonFilename;

  /**
   * Thread function to capture frames.
   * Simulates capturing video frames and storing them in a queue.
   */
  void captureFrames();

  /**
   * Thread function to process frames.
   * Simulates processing video frames from a queue.
   */
  void processFrames();

  /**
   * @brief Write a JSON file with meta data about the video file
   */
  void writeJsonSidecarFile();
};
