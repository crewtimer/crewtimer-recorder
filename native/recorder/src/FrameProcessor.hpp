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

  /**
   * Constructor for FrameProcessor.
   * Initializes and starts capture and process threads.
   */
  FrameProcessor(const std::string directory, const std::string prefix,
                 std::shared_ptr<VideoRecorder> videoRecorder,
                 int durationSecs);

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

private:
  StatusInfo statusInfo;
  std::string errorMessage;
  const std::string directory;
  const std::string prefix;
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
};
