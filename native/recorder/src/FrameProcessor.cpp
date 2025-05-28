#include "FrameProcessor.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "SystemEventQueue.hpp"
#include "VideoUtils.hpp"
using namespace std::chrono;

static int16_t getTimezoneOffset()
{
  // Get the current time in the system's local timezone
  auto now = std::chrono::system_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm = *std::localtime(&now_c);

  // Get the current time in UTC
  std::tm utc_tm = *std::gmtime(&now_c);

  // Calculate the difference in seconds
  // -1 tm_isdst ensures the system determines whether DST is in effect
  local_tm.tm_isdst = -1;
  std::time_t local_time = std::mktime(&local_tm);

  utc_tm.tm_isdst = -1;
  std::time_t utc_time = std::mktime(&utc_tm);
  double offset_seconds = std::difftime(local_time, utc_time);

  // Convert the difference to minutes
  auto offset_minutes = static_cast<int16_t>(offset_seconds / 60);
  return offset_minutes;
}

FrameProcessor::FrameProcessor(const std::string directory,
                               const std::string prefix,
                               std::shared_ptr<VideoRecorder> videoRecorder,
                               int durationSecs, FRectangle cropArea,
                               Guide guide, bool addTimeOverlay)
    : directory(directory), prefix(prefix), cropArea(cropArea),
      pxCropArea(Rectangle(0, 0, 0, 0)), guide(guide), addTimeOverlay(addTimeOverlay),
      videoRecorder(videoRecorder), durationSecs(durationSecs), running(true),
      processThread(&FrameProcessor::processFrames, this)
{
  errorMessage = "";
  statusInfo.recording = false;
  statusInfo.error = "";
  statusInfo.filename = "";
  statusInfo.width = 0;
  statusInfo.height = 0;
  statusInfo.fps = 0;
  statusInfo.frameBacklog = 0;
  statusInfo.lastTsMilli = 0;
  tzOffset = getTimezoneOffset();
}

FrameProcessor::~FrameProcessor() { stop(); }

void FrameProcessor::stop()
{
  errorMessage = "";
  running = false;
  frameAvailable.notify_all();

  if (processThread.joinable())
    processThread.join();
  while (!frameQueue.empty())
  {
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

FrameProcessor::StatusInfo FrameProcessor::getStatus()
{
  statusInfo.recording = running;
  statusInfo.error = errorMessage;
  return statusInfo;
}

void FrameProcessor::addFrame(FramePtr video_frame)
{
  std::unique_lock<std::mutex> lock(queueMutex);
  if (!running)
  {
    return;
  }
  lastFrame = video_frame;
  frameQueue.push(video_frame);
  frameAvailable.notify_one();
}

FramePtr FrameProcessor::getLastFrame()
{
  std::unique_lock<std::mutex> lock(queueMutex);
  return lastFrame;
}

void FrameProcessor::writeJsonSidecarFile()
{
  if (frameCount == 0)
  {
    return;
  }

  std::ofstream jsonFile(jsonFilename);
  if (!jsonFile)
  {
    errorMessage =
        "Error: Could not open the file '" + jsonFilename + "' for writing.";
    running = false;
    return;
  }

  jsonFile << std::fixed << std::setprecision(7) << "{\n"
           << "  \"file\": {\n"
           << "    \"startTs\": \"" << startTs / 1e7 << "\",\n"
           << "    \"stopTs\": \"" << lastTs / 1e7 << "\",\n"
           << "    \"numFrames\": " << frameCount << ",\n"
           << "    \"tzOffset\": " << tzOffset << "\n"
           << "  },\n"
           << "  \"source\": {\n"
           << "    \"width\": "
           << (pxCropArea.width ? pxCropArea.width : lastXres) << ",\n"
           << "    \"height\": "
           << (pxCropArea.height ? pxCropArea.height : lastYres) << "\n"
           << "  },\n"
           << "  \"guide\": {\n"
           << "    \"pt1\": " << guide.pt1 << ",\n"
           << "    \"pt2\": " << guide.pt2 << "\n"
           << "  }\n"
           << "}\n";
  jsonFile.close();
}

void FrameProcessor::processFrames()
{
  SystemEventQueue::push("fproc", "Starting frame processor");
  int count = 0;
  auto start = high_resolution_clock::now();
  auto useEmbeddedTimestamp = true;
  lastXres = 0;
  lastYres = 0;
  lastFPS = 0;
  frameCount = 0;
  int64_t keyFrameInterval = videoRecorder->getKeyFrameInterval();

  while (running)
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    frameAvailable.wait(lock,
                        [this]
                        { return !frameQueue.empty() || !running; });
    while (running && !frameQueue.empty())
    {
      auto video_frame = frameQueue.front();
      frameQueue.pop();

      statusInfo.frameBacklog = frameQueue.size();
      lock.unlock();
      if (video_frame->xres == 0 || video_frame->yres == 0)
      {
        std::cerr << "Null frame received" << std::endl;
        continue;
      }
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

      statusInfo.lastTsMilli = video_frame->timestamp / 10000;

      // The video review app may have trouble reading the last video frame when there is not a multiple of gop_size frames
      // Exact cause unknown.
      if (propChange || count == 0 ||
          (useEmbeddedTimestamp && video_frame->timestamp >= nextStartTime && frameCount % keyFrameInterval == 0) ||
          (okToSplit && splitRequested && frameCount % keyFrameInterval == 0))
      {
        count++;
        if (frameCount > 0)
        {
          writeJsonSidecarFile();
          videoRecorder->stop();
        }

        const auto ts100ns = video_frame->timestamp;
        startTs = ts100ns;
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
        jsonFilename = directory + "/" + filename + ".json";
        statusInfo.filename = filename;
        statusInfo.fps = fps;
        statusInfo.width = video_frame->xres;
        statusInfo.height = video_frame->yres;

        pxCropArea.x = std::round((cropArea.x * (video_frame->xres) / 4)) * 4;
        int cropWidth = cropArea.width * video_frame->xres;
        cropWidth = std::min(video_frame->xres - pxCropArea.x, cropWidth);
        pxCropArea.width = int(cropWidth / 4) * 4; // trunc to mult of 4

        pxCropArea.y = std::round((cropArea.y * (video_frame->yres) / 4)) * 4;
        int cropHeight = cropArea.height * video_frame->yres;
        cropHeight = std::min(video_frame->yres - pxCropArea.y, cropHeight);
        pxCropArea.height = int(cropHeight / 4) * 4; // trunc to mult of 4

        std::cout << "cropArea: " << cropArea.x << "," << cropArea.y << ","
                  << cropArea.width << "," << cropArea.height << std::endl;

        std::cout << "pxCropArea: " << pxCropArea.x << "," << pxCropArea.y
                  << "," << pxCropArea.width << "," << pxCropArea.height
                  << std::endl;

        const auto err = videoRecorder->openVideoStream(
            directory, filename,
            pxCropArea.width ? pxCropArea.width : video_frame->xres,
            pxCropArea.height ? pxCropArea.height : video_frame->yres, fps);
        if (!err.empty())
        {
          errorMessage = err;
          running = false;
          frameCount = 0;
          break;
        }

        std::cerr << "File: " << filename << " " << video_frame->xres << "x"
                  << video_frame->yres << " fps=" << fps
                  << " prior_fc=" << frameCount
                  << " Backlog=" << frameQueue.size() << std::endl;
        frameCount = 0;
      }

      auto cropped = video_frame;
      // std::cerr << "vstride: " << std::dec << video_frame->stride
      //           << "ptr: " << std::hex << (void *)(video_frame->data) << std::dec
      //           << std::endl;
      if (pxCropArea.width && pxCropArea.height)
      {
        // std::cout << "Cropping frame (" << pxCropArea.x << "," << pxCropArea.y
        //           << ")" << pxCropArea.width << "x" << pxCropArea.height
        //           << std::endl;
        cropped = cropFrame(video_frame, pxCropArea.x, pxCropArea.y,
                            pxCropArea.width, pxCropArea.height);

        if (!cropped)
        {
          std::cout << "Crop (" << pxCropArea.x << "," << pxCropArea.y
                    << ")" << pxCropArea.width << "x" << pxCropArea.height << " from frame=" << video_frame->xres << "x" << video_frame->yres
                    << std::endl;
          std::cerr << "Frame crop failed." << std::endl;
          cropped = video_frame; // continue with full frame
        }
        // std::cerr << " stride: " << std::dec << cropped->stride
        //           << "ptr: " << std::hex << (void *)(cropped->data) << std::dec
        //           << std::endl;
      }

      encodeTimestamp(cropped->data, cropped->stride, video_frame->timestamp);
      if (addTimeOverlay)
      {
        overlayTime(cropped->data, cropped->stride, video_frame->timestamp);
      }

      auto err = videoRecorder->writeVideoFrame(cropped);
      if (!err.empty())
      {
        errorMessage = err;
        running = false;
        frameCount = 0;
        break;
      }
      lastTs = video_frame->timestamp;
      frameCount++;

      lock.lock();
      if (frameQueue.size() > 500)
      {
        SystemEventQueue::instance().push(
            "fproc", "Frame queue overflow, discarding frames");
        frameQueue = std::queue<FramePtr>();
      }
    }
  }

  if (frameCount > 0)
  {
    writeJsonSidecarFile();
    videoRecorder->stop();
  }
}
