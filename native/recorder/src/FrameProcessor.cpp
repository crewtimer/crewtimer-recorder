#include "FrameProcessor.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace std::chrono;

FrameProcessor::FrameProcessor(const std::string directory,
                               const std::string prefix,
                               std::shared_ptr<VideoRecorder> videoRecorder,
                               int durationSecs)
    : directory(directory), prefix(prefix), videoRecorder(videoRecorder),
      durationSecs(durationSecs), running(true),
      captureThread(&FrameProcessor::captureFrames, this),
      processThread(&FrameProcessor::processFrames, this) {}

FrameProcessor::~FrameProcessor() { stop(); }

void FrameProcessor::stop() {
  running = false;
  frameAvailable.notify_all();
  if (captureThread.joinable())
    captureThread.join();
  if (processThread.joinable())
    processThread.join();
  while (!frameQueue.empty()) {
    frameQueue.pop();
  }

  videoRecorder->stop();
}

/**
 * @brief Triggers the frame processor to close the currently open file and
 * open a new file.
 */
void FrameProcessor::splitFile() { splitRequested = true; }

void FrameProcessor::addFrame(FramePtr video_frame) {
  std::unique_lock<std::mutex> lock(queueMutex);
  frameQueue.push(video_frame);
  frameAvailable.notify_one();
}
void FrameProcessor::captureFrames() {
  while (running) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(1000)); // Simulate frame capture time
    // std::unique_lock<std::mutex> lock(queueMutex);
    // frameQueue.push(Frame(frameCounter++));
    // frameAvailable.notify_one();
  }
}

void FrameProcessor::processFrames() {
  std::cout << "Starting frame processor" << std::endl;
  int64_t lastTS = 0;
  int64_t frameCount = 0;
  int count = 0;
  auto start = high_resolution_clock::now();
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
      if (nextStartTime == 0) {
        uint64_t complete_periods =
            video_frame->timestamp / (durationSecs * 10000000);

        nextStartTime = complete_periods * durationSecs * 10000000;
        // std::cout << video_frame->xres << "x" << video_frame->yres
        //           << " fps=" << fps << std::endl;
        //   printf("%d x %d, FourCC=0x%08x, UYVY=0x%08x\n", video_frame->xres,
        //          video_frame->yres, video_frame->FourCC,
        //          NDI_LIB_FOURCC('U', 'Y', 'V', 'Y'));
      }

      // Ensure we do not split within the first second of a recording as the
      // filename will be the same when resuming after the split
      auto now = high_resolution_clock::now();
      auto elapsed = duration_cast<milliseconds>(now - start);
      auto okToSplit = elapsed.count() > 1200;

      if (video_frame->timestamp >= nextStartTime ||
          (okToSplit && splitRequested)) {
        if (frameCount > 0) {
          videoRecorder->stop();
        }

        auto utc_milliseconds = nextStartTime / 10000;
        splitRequested = false;
        start = high_resolution_clock::now();
        if (video_frame->timestamp >= nextStartTime) {
          nextStartTime += durationSecs * 10000000;
        }

        auto now = system_clock::now() + milliseconds(100);

        // Convert the time_point to a time_t object, representing system time
        std::time_t now_time_t = system_clock::to_time_t(now);

        // Convert time_t to tm as local time
        std::tm *now_tm = std::localtime(&now_time_t);

        // Construct a filename with the time embedded
        std::stringstream ss;
        ss << prefix << std::put_time(now_tm, "%H_%M_%S");

        std::string filename = ss.str();

        const auto err = videoRecorder->openVideoStream(
            directory, filename, video_frame->xres, video_frame->yres, fps);
        if (err) {
          running = false;
          break;
        }

        auto priorMilli = (5000 + lastTS) / 10000;
        auto currMilli = (5000 + video_frame->timestamp) / 10000;
        std::cout << "File: " << filename << " " << video_frame->xres << "x"
                  << video_frame->yres << " fps=" << fps
                  << " last: " << std::setw(2) << std::setfill('0')
                  << (priorMilli / (60 * 60 * 1000)) % 24 << ":" << std::setw(2)
                  << std::setfill('0') << (priorMilli / (60 * 1000)) % 60 << ":"
                  << std::setw(2) << std::setfill('0')
                  << (priorMilli / (1000)) % 60 << ":" << std::setw(3)
                  << std::setfill('0') << priorMilli % 1000
                  << " next: " << std::setw(2) << std::setfill('0')
                  << (currMilli / (60 * 60 * 1000)) % 24 << ":" << std::setw(2)
                  << std::setfill('0') << (currMilli / (60 * 1000)) % 60 << ":"
                  << std::setw(2) << std::setfill('0')
                  << (currMilli / (1000)) % 60 << ":" << std::setw(3)
                  << std::setfill('0') << currMilli % 1000
                  << " fc=" << frameCount << " Backlog=" << frameQueue.size()
                  << std::endl;
        frameCount = 0;
      }
      lastTS = video_frame->timestamp;
      videoRecorder->writeVideoFrame(video_frame);
      frameCount++;

      lock.lock();
      if (frameQueue.size() > 500) {
        std::cerr << "Frame queue overflow, discarding frames" << std::endl;
        frameQueue = std::queue<FramePtr>();
      }
    }
  }
}