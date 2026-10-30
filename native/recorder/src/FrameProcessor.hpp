#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <set>
#include <thread>

#include "VideoRecorder.hpp"
#include "VideoUtils.hpp"

/**
 * Class responsible for processing video frames.
 */
class FrameProcessor
{

public:
  struct StatusInfo
  {
    bool recording = false;
    std::string error;
    std::string filename;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t lastTsMilli = 0;
    // Camera time at which the current file ends and the next slice starts
    std::uint64_t sliceEndMilli = 0;
    float fps = 60;
    float measuredFps = 0;
    // Camera timestamp minus PC arrival time (least-delayed frame per window); valid when measuredFps > 0
    float clockOffsetMs = 0;
    // measuredFps fell below 90% of fps and has not yet recovered to 95%
    bool lowFrameRate = false;
    std::uint32_t frameBacklog = 0;

    friend std::ostream &operator<<(std::ostream &os, const StatusInfo &info)
    {
      os << "{recording=" << info.recording
         << ", error='" << info.error
         << "', filename='" << info.filename
         << "', width=" << info.width
         << ", height=" << info.height
         << ", lastTsMilli=" << info.lastTsMilli
         << ", sliceEndMilli=" << info.sliceEndMilli
         << ", fps=" << info.fps
         << ", measuredFps=" << info.measuredFps
         << ", clockOffsetMs=" << info.clockOffsetMs
         << ", lowFrameRate=" << info.lowFrameRate
         << ", frameBacklog=" << info.frameBacklog << "}";
      return os;
    }
  };

  struct Rectangle
  {
    int x;
    int y;
    int width;
    int height;
    Rectangle(int x, int y, int width, int height)
        : x(x), y(y), width(width), height(height) {}
  };

  struct FRectangle
  {
    float x;
    float y;
    float width;
    float height;
    FRectangle(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}
  };

  struct Guide
  {
    float pt1;
    float pt2;
    Guide() : pt1(0), pt2(0) {}
  };

  /**
   * Constructor for FrameProcessor.
   * Initializes and starts capture and process threads.
   */
  FrameProcessor(const std::string directory, const std::string prefix,
                 std::shared_ptr<VideoRecorder> videoRecorder, int durationSecs,
                 FRectangle cropArea, Guide guide, bool overlap = false);

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
   * @brief Triggers the frame processor to close the file holding the latest video, once
   * it has TimingConstants::MinimumNewVideo100ns of new video. The new file closes again
   * once it has that much new video, so it can be reviewed straight away.
   *
   * A request is dropped when the latest video is already in a short file, or in the
   * short file a waiting split will make: the file it asks to close is closing soon.
   * @return false when the request was dropped
   */
  bool splitFile();

  /**
   * @brief Make sure a file that closes soon owns a crossing at ts100ns, splitting
   * only when the file that would own it is a long one. Each file owns its video up
   * to the middle of the overlap with its neighbours, as Video Review shows it. A
   * crossing owned by a closed file, by the first second of a short file, or by the
   * short file a waiting split makes, needs no split. A crossing ahead of the video
   * splits once the video reaches it.
   * @return false when no split was needed
   */
  bool splitAt(uint64_t ts100ns);

  StatusInfo getStatus();

  /**
   * @brief Get the Last Frame object received by the frame processor
   *
   * @return FramePtr
   */
  FramePtr getLastFrame();

private:
  StatusInfo statusInfo;
  std::string errorMessage = "";
  const std::string directory;
  const std::string prefix;
  FRectangle cropArea;
  Rectangle pxCropArea;
  Guide guide;

  std::shared_ptr<VideoRecorder> videoRecorder;
  // Split files keep the encoder open and overlap; the recorder reports each finished
  // file, and its sidecar is written then.
  const bool overlap;
  uint64_t durationSecs;
  uint64_t nextStartTime = 0;
  // Guards the split request and the open file's owned window, which split requests
  // from other threads are judged by.
  std::mutex splitMutex;
  /// Crossings, in time order, each waiting for a split once the video reaches it.
  std::set<uint64_t> crossings;
  /// Timestamp of the latest frame processed, which a split request is for.
  uint64_t processedTs = 0;
  /// The open file owns video from here; earlier video is owned by closed files.
  uint64_t ownedFromTs = 0;
  /// A short file, made by a requested split, owns video before this; 0 for a long file.
  uint64_t shortOwnedUntilTs = 0;
  /// A short file holds video before this; 0 for a long file.
  uint64_t shortEndTs = 0;
  /// A long file split only for a crossing in the short file after it is held back:
  /// it appears in the folder just after the short file and is not reported, so Video
  /// Review does not open a file of no interest first.
  std::string heldFilename;
  std::queue<FramePtr> frameQueue;        ///< Queue to hold the frames.
  std::mutex queueMutex;                  ///< Mutex for synchronizing access to the frame queue.
  std::condition_variable frameAvailable; ///< Condition variable to notify
                                          ///< when frames are available.
  std::mutex statusMutex;                 ///< Mutex to protect statusInfo access.
  std::atomic<bool> running;              ///< Atomic flag to control the running state of threads.
  std::thread processThread;              ///< Threads for capturing and processing frames.
  FramePtr lastFrame;

  // JSON file props
  int64_t frameCount = 0;
  uint64_t startTs;
  uint64_t lastTs;
  int16_t tzOffset;
  int lastXres = 0;
  int lastYres = 0;
  int lastSensorXres = 0;
  int lastSensorYres = 0;
  int lastRotation = 0;
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
  void writeJsonSidecarFile(const std::string &path, uint64_t startTs, uint64_t stopTs,
                            int64_t numFrames);

  /**
   * @brief Finish the current file, or with overlap every file, and its sidecar
   */
  void closeRecording();

  /**
   * @brief Tell the renderer, and so Video Review, that a file is in place
   */
  void reportFileClosed(const std::string &directory, const std::string &filename);

  /**
   * @brief A waiting crossing within window100ns of ts100ns, whose split soon closes
   * the file holding ts100ns or starts a short file holding it. Called with splitMutex
   * held.
   */
  bool waitingSplitCovers(uint64_t ts100ns, uint64_t window100ns) const;
};
