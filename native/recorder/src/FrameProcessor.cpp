#include "FrameProcessor.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "SystemEventQueue.hpp"
using namespace std::chrono;

FrameProcessor::FrameProcessor(const std::string directory,
                               const std::string prefix,
                               std::shared_ptr<VideoRecorder> videoRecorder,
                               int durationSecs)
    : directory(directory), prefix(prefix), videoRecorder(videoRecorder),
      durationSecs(durationSecs), running(true),
      processThread(&FrameProcessor::processFrames, this) {
  errorMessage = "";
}

FrameProcessor::~FrameProcessor() { stop(); }

void FrameProcessor::stop() {
  errorMessage = "";
  running = false;
  frameAvailable.notify_all();

  if (processThread.joinable())
    processThread.join();
  while (!frameQueue.empty()) {
    frameQueue.pop();
  }

  lastFrame = nullptr;
  videoRecorder = nullptr;
}

/**
 * @brief Triggers the frame processor to close the currently open file and
 * open a new file.
 */
void FrameProcessor::splitFile() { splitRequested = true; }

FrameProcessor::StatusInfo FrameProcessor::getStatus() {
  statusInfo.recording = running;
  statusInfo.error = errorMessage;
  return statusInfo;
}

void FrameProcessor::addFrame(FramePtr video_frame) {
  std::unique_lock<std::mutex> lock(queueMutex);
  if (!running) {
    return;
  }
  lastFrame = video_frame;
  frameQueue.push(video_frame);
  frameAvailable.notify_one();
}

FramePtr FrameProcessor::getLastFrame() {
  std::unique_lock<std::mutex> lock(queueMutex);
  return lastFrame;
}

void FrameProcessor::processFrames() {
  SystemEventQueue::push("fproc", "Starting frame processor");
  int64_t frameCount = 0;
  int count = 0;
  auto start = high_resolution_clock::now();
  auto useEmbeddedTimestamp = true;
  int lastXres = 0;
  int lastYres = 0;
  float lastFPS = 0;

  while (running) {
    std::unique_lock<std::mutex> lock(queueMutex);
    frameAvailable.wait(lock,
                        [this] { return !frameQueue.empty() || !running; });
    while (running && !frameQueue.empty()) {
      auto video_frame = frameQueue.front();
      frameQueue.pop();
      lock.unlock();
      const auto fps =
          (float)video_frame->frame_rate_N / (float)video_frame->frame_rate_D;

      // Ensure we do not split within the first second of a recording as the
      // filename will be the same when resuming after the split
      auto now = high_resolution_clock::now();
      auto elapsed = duration_cast<milliseconds>(now - start);
      auto okToSplit = elapsed.count() > 1200;
      auto propChange = lastXres != video_frame->xres ||
                        lastYres != video_frame->yres || lastFPS != fps;
      lastXres = video_frame->xres;
      lastYres = video_frame->yres;
      lastFPS = fps;

      if (propChange || count == 0 ||
          (useEmbeddedTimestamp && video_frame->timestamp >= nextStartTime) ||
          (okToSplit && splitRequested)) {
        count++;
        if (frameCount > 0) {
          videoRecorder->stop();
        }

        const auto ts100ns = video_frame->timestamp;
        uint64_t complete_periods = ts100ns / (durationSecs * 10000000);
        nextStartTime = (1 + complete_periods) * durationSecs * 10000000;

        splitRequested = false;
        start = high_resolution_clock::now();

        // Construct a filename with the time embedded
        const auto milli = (5000 + ts100ns) / 10000;

        // Convert utc milliseconds to time_point of system clock
        time_point<system_clock> tp =
            time_point<system_clock>(milliseconds(milli));

        // Convert to system time_t for conversion to tm structure
        std::time_t raw_time = system_clock::to_time_t(tp);

        // Convert to local time
        std::tm *local_time = std::localtime(&raw_time);
        std::stringstream ss;
        ss << prefix << std::put_time(local_time, "%Y%m%d_%H%M%S");

        std::string filename = ss.str();
        statusInfo.filename = filename;
        statusInfo.fps = fps;
        statusInfo.width = video_frame->xres;
        statusInfo.height = video_frame->yres;

        const auto err = videoRecorder->openVideoStream(
            directory, filename, video_frame->xres, video_frame->yres, fps);
        if (!err.empty()) {
          errorMessage = err;
          running = false;
          break;
        }

        std::cout << "File: " << filename << " " << video_frame->xres << "x"
                  << video_frame->yres << " fps=" << fps
                  << " prior_fc=" << frameCount
                  << " Backlog=" << frameQueue.size() << std::endl;
        frameCount = 0;
      }

      auto err = videoRecorder->writeVideoFrame(video_frame);
      if (!err.empty()) {
        errorMessage = err;
        running = false;
        break;
      }
      frameCount++;

      lock.lock();
      if (frameQueue.size() > 500) {
        SystemEventQueue::instance().push(
            "fproc", "Frame queue overflow, discarding frames");
        frameQueue = std::queue<FramePtr>();
      }
    }
  }
}
