#include "FrameProcessor.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "SystemEventQueue.hpp"
#include "TimingConstants.hpp"
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
                               Guide guide, bool overlap)
    : directory(directory), prefix(prefix), cropArea(cropArea),
      pxCropArea(Rectangle(0, 0, 0, 0)), guide(guide),
      videoRecorder(videoRecorder),
      overlap(overlap && videoRecorder->supportsSplit()),
      durationSecs(durationSecs), running(true),
      processThread(&FrameProcessor::processFrames, this)
{
  errorMessage = "";
  statusInfo.recording = false;
  statusInfo.error = "";
  statusInfo.filename = "";
  statusInfo.width = 0;
  statusInfo.height = 0;
  statusInfo.fps = 0;
  statusInfo.measuredFps = 0;
  statusInfo.clockOffsetMs = 0;
  statusInfo.lowFrameRate = false;
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

// With overlap the next file reaches back to a short file's split, so owns from about
// halfway through it.
static uint64_t shortOwned100ns(bool overlap)
{
  return overlap ? TimingConstants::MinimumNewVideo100ns / 2 : TimingConstants::MinimumNewVideo100ns;
}

bool FrameProcessor::waitingSplitCovers(uint64_t ts100ns, uint64_t window100ns) const
{
  // Video just before the crossing is in a file closing at its split, or, with
  // overlap, the short file it starts. A crossing further off, such as one from
  // another day, does not cover it.
  const uint64_t from = ts100ns > window100ns ? ts100ns - window100ns : 0;
  auto near = crossings.upper_bound(from);
  return near != crossings.end() && *near < ts100ns + window100ns;
}

bool FrameProcessor::splitFile()
{
  std::lock_guard<std::mutex> lock(splitMutex);
  const uint64_t ts100ns = processedTs;
  // A waiting split's short file holds the MinimumNewVideo100ns after its crossing.
  const bool covered = ts100ns == 0 || ts100ns < shortEndTs ||
                       waitingSplitCovers(ts100ns, TimingConstants::MinimumNewVideo100ns);
  if (covered)
  {
    SystemEventQueue::push("fproc", "Split request ignored; the latest video is in a file that closes soon");
    return false;
  }
  crossings.insert(ts100ns);
  return true;
}

bool FrameProcessor::splitAt(uint64_t ts100ns)
{
  std::lock_guard<std::mutex> lock(splitMutex);
  if (ts100ns < ownedFromTs || ts100ns < shortOwnedUntilTs ||
      waitingSplitCovers(ts100ns, shortOwned100ns(overlap)))
  {
    return false;
  }
  crossings.insert(ts100ns);
  return true;
}

FrameProcessor::StatusInfo FrameProcessor::getStatus()
{
  std::lock_guard<std::mutex> s(statusMutex);
  // make a copy to avoid holding the mutex while the caller inspects the structure
  StatusInfo copy = statusInfo;
  copy.recording = running;
  copy.error = errorMessage;
  return copy;
}

void FrameProcessor::addFrame(FramePtr video_frame)
{
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    if (!running)
    {
      return;
    }
    lastFrame = video_frame;
    frameQueue.push(video_frame);
  }
  // Notify after unlocking to avoid waking the worker only to contend on the lock.
  frameAvailable.notify_one();
}

FramePtr FrameProcessor::getLastFrame()
{
  std::unique_lock<std::mutex> lock(queueMutex);
  return lastFrame;
}

void FrameProcessor::closeRecording()
{
  if (!overlap)
  {
    writeJsonSidecarFile(jsonFilename, startTs, lastTs, frameCount);
  }
  // With overlap the recorder writes sidecars through onFileClosed as files finish.
  videoRecorder->stop();
}

void FrameProcessor::reportFileClosed(const std::string &directory, const std::string &filename)
{
#ifndef STANDALONE
  // The renderer tells Video Review, so it lists the file without waiting to poll.
  nlohmann::json msg = {{"directory", directory}, {"filename", filename}};
  sendMessageToRenderer("file-closed", std::make_shared<nlohmann::json>(msg));
#endif
}

void FrameProcessor::writeJsonSidecarFile(const std::string &path, uint64_t startTs,
                                          uint64_t stopTs, int64_t numFrames)
{
  constexpr double LANDSCAPE_ROLLING_SHUTTER_SCAN_TIME_MS = 13.8;

  if (numFrames == 0)
  {
    return;
  }

  std::ofstream jsonFile(path);
  if (!jsonFile)
  {
    errorMessage =
        "Error: Could not open the file '" + path + "' for writing.";
    running = false;
    return;
  }

  const int recordedWidth = pxCropArea.width ? pxCropArea.width : lastXres;
  const int recordedHeight = pxCropArea.height ? pxCropArea.height : lastYres;
  const int cropX = pxCropArea.width ? pxCropArea.x : 0;
  const int cropY = pxCropArea.height ? pxCropArea.y : 0;

  jsonFile << std::fixed << std::setprecision(7) << "{\n"
           << "  \"file\": {\n"
           << "    \"startTs\": \"" << startTs / 1e7 << "\",\n"
           << "    \"stopTs\": \"" << stopTs / 1e7 << "\",\n"
           << "    \"numFrames\": " << numFrames << ",\n"
           << "    \"tzOffset\": " << tzOffset << "\n"
           << "  },\n"
           << "  \"source\": {\n"
           << "    \"width\": " << recordedWidth << ",\n"
           << "    \"height\": " << recordedHeight << ",\n"
           << "    \"originalWidth\": " << lastXres << ",\n"
           << "    \"originalHeight\": " << lastYres << ",\n"
           << "    \"crop\": {\n"
           << "      \"x\": " << cropX << ",\n"
           << "      \"y\": " << cropY << ",\n"
           << "      \"width\": " << recordedWidth << ",\n"
           << "      \"height\": " << recordedHeight << "\n"
           << "    }\n"
           << "  },\n"
           << "  \"sensor\": {\n";
  if (lastSensorXres > lastSensorYres)
  {
    const bool rotated = lastRotation != 0;
    const char *direction = lastRotation == 90 ? "right-to-left"
                            : lastRotation == -90 ? "left-to-right"
                                                  : "top-to-bottom";
    const char *referenceAxis = rotated ? "x" : "y";
    const double referencePosition =
        static_cast<double>(rotated ? lastXres : lastYres) / 2.0;
    jsonFile << "    \"nativeWidth\": " << lastSensorXres << ",\n"
             << "    \"nativeHeight\": " << lastSensorYres << ",\n"
             << "    \"sourceRotationDegrees\": " << lastRotation << ",\n"
             << "    \"rollingShutter\": {\n"
             << "      \"direction\": \"" << direction << "\",\n"
             << "      \"scanTimeMs\": " << LANDSCAPE_ROLLING_SHUTTER_SCAN_TIME_MS << ",\n"
             << "      \"frameTimeReference\": {\n"
             << "        \"coordinateSpace\": \"original-image\",\n"
             << "        \"axis\": \"" << referenceAxis << "\",\n"
             << "        \"position\": " << referencePosition << "\n"
             << "      }\n"
             << "    }\n";
  }
  else
  {
    jsonFile << "    \"nativeWidth\": " << lastSensorXres << ",\n"
             << "    \"nativeHeight\": " << lastSensorYres << ",\n"
             << "    \"sourceRotationDegrees\": " << lastRotation << ",\n"
             << "    \"rollingShutter\": null\n";
  }
  jsonFile << "  },\n"
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
  auto useEmbeddedTimestamp = true;
  lastXres = 0;
  lastYres = 0;
  lastSensorXres = 0;
  lastSensorYres = 0;
  lastRotation = 0;
  lastFPS = 0;
  frameCount = 0;
  int64_t keyFrameInterval = videoRecorder->getKeyFrameInterval();
  // Actual frame rate from the camera timestamps of processed frames, over ~1 s windows
  constexpr uint64_t rateWindow100ns = 10000000;
  uint64_t rateWindowStart = 0;
  uint32_t rateWindowFrames = 0;
  // Delay only lowers (camera time - arrival time), so the window's largest value is
  // the least-delayed frame: the best estimate of the camera clock offset.
  int64_t windowBestOffset100ns = INT64_MIN;
  // measuredFps only updates when a frame completes a window, so a source that
  // stops entirely is caught by the time since the last video frame instead.
  constexpr auto stallThreshold = seconds(2);
  auto lastVideoFrameTime = steady_clock::now();
  heldFilename.clear();
  heldClosedFile = {};
  // Called on this thread from inside the recorder once a file is in place; cleared
  // before the thread ends.
  videoRecorder->onFileClosed = [this](const RecordedFile &file)
  {
    // Without overlap closeRecording wrote the sidecar before stopping the recorder.
    if (overlap)
    {
      writeJsonSidecarFile(file.directory + "/" + file.filename + ".json",
                           file.firstTs100ns, file.lastTs100ns, file.frames);
    }
    if (!heldFilename.empty() && file.filename == heldFilename)
    {
      heldFilename.clear();
      heldClosedFile = {file.directory, file.filename};
      return;
    }
    // Video Review lists a held file along with this one when it refreshes the folder.
    heldClosedFile = {};
    reportFileClosed(file.directory, file.filename);
  };

  while (running)
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    frameAvailable.wait_for(lock, milliseconds(500),
                            [this]
                            { return !frameQueue.empty() || !running; });
    const auto sinceLastFrame = steady_clock::now() - lastVideoFrameTime;
    if (running && frameQueue.empty() && sinceLastFrame > stallThreshold)
    {
      std::string stallEvent;
      {
        std::lock_guard<std::mutex> s(statusMutex);
        if (!statusInfo.lowFrameRate)
        {
          statusInfo.lowFrameRate = true;
          statusInfo.measuredFps = 0;
          stallEvent = "Warning: Low data rate: no frames for " +
                       std::to_string(duration_cast<seconds>(sinceLastFrame).count()) + " s";
        }
      }
      // Measure afresh once frames resume rather than across the stall
      rateWindowStart = 0;
      if (!stallEvent.empty())
        SystemEventQueue::push("fproc", stallEvent);
    }
    while (running && !frameQueue.empty())
    {
      auto video_frame = frameQueue.front();
      frameQueue.pop();
      {
        // update frame backlog under status mutex to avoid races on statusInfo
        std::lock_guard<std::mutex> s(statusMutex);
        statusInfo.frameBacklog = frameQueue.size();
      }
      lock.unlock();
      // If a source-disconnected frame is received, close the current file immediately.
      if (video_frame->frameType == Frame::FrameType::SOURCE_DISCONNECTED)
      {
        SystemEventQueue::instance().push("fproc", "Source disconnected - splitting file immediately");
        if (frameCount > 0)
        {
          closeRecording();
        }
        frameCount = 0;
        count = 0;
        rateWindowStart = 0;
        {
          std::lock_guard<std::mutex> s(statusMutex);
          statusInfo.filename.clear();
          statusInfo.measuredFps = 0;
          statusInfo.clockOffsetMs = 0;
          statusInfo.lowFrameRate = false;
        }
        lock.lock();
        continue;
      }
      if (video_frame->frameType != Frame::FrameType::VIDEO)
      {
        // Ignore non-video frames; relock before re-evaluating the queue predicate.
        lock.lock();
        continue;
      }
      if (video_frame->xres == 0 || video_frame->yres == 0)
      {
        std::cerr << "Null frame received" << std::endl;
        lock.lock();
        continue;
      }

      lastVideoFrameTime = steady_clock::now();

      // Apply the global capture-time correction once, before deriving file
      // metadata, sidecar timestamps, split boundaries, or encoded output.
      if (video_frame->timestamp >= TimingConstants::GlobalTimestampCorrection100ns)
      {
        video_frame->timestamp -= TimingConstants::GlobalTimestampCorrection100ns;
      }
      const auto fps =
          (float)video_frame->frame_rate_N / (float)video_frame->frame_rate_D;

      auto propChange = lastXres != video_frame->xres ||
                        lastYres != video_frame->yres || lastFPS != fps;
      lastXres = video_frame->xres;
      lastYres = video_frame->yres;
      lastSensorXres = video_frame->sensorXres ? video_frame->sensorXres : video_frame->xres;
      lastSensorYres = video_frame->sensorYres ? video_frame->sensorYres : video_frame->yres;
      lastRotation = video_frame->rotation;
      lastFPS = fps;

      float measuredFps = -1;
      int64_t clockOffset100ns = INT64_MIN;
      if (video_frame->receivedTs100ns)
      {
        windowBestOffset100ns = std::max(
            windowBestOffset100ns,
            int64_t(video_frame->timestamp) - int64_t(video_frame->receivedTs100ns));
      }
      // A timestamp going backwards (camera clock reset) restarts the window
      if (rateWindowStart == 0 || video_frame->timestamp < rateWindowStart)
      {
        rateWindowStart = video_frame->timestamp;
        rateWindowFrames = 0;
        windowBestOffset100ns = INT64_MIN;
      }
      else
      {
        rateWindowFrames++;
        const uint64_t span = video_frame->timestamp - rateWindowStart;
        if (span >= rateWindow100ns)
        {
          measuredFps = float(rateWindowFrames * 1e7 / double(span));
          clockOffset100ns = windowBestOffset100ns;
          rateWindowStart = video_frame->timestamp;
          rateWindowFrames = 0;
          windowBestOffset100ns = INT64_MIN;
        }
      }

      std::string rateEvent;
      {
        std::lock_guard<std::mutex> s(statusMutex);
        statusInfo.lastTsMilli = video_frame->timestamp / 10000;
        statusInfo.sliceEndMilli = nextStartTime / 10000;
        if (measuredFps >= 0)
        {
          // Below 90% of the declared rate means frames are being lost.  Recover
          // at 95% so a rate hovering near the limit does not flood the log.
          std::stringstream msg;
          msg << std::fixed << std::setprecision(2) << measuredFps << " fps of expected " << fps << " fps";
          if (!statusInfo.lowFrameRate && measuredFps < 0.9f * fps)
          {
            statusInfo.lowFrameRate = true;
            rateEvent = "Warning: Low data rate: " + msg.str();
          }
          else if (statusInfo.lowFrameRate && measuredFps >= 0.95f * fps)
          {
            statusInfo.lowFrameRate = false;
            rateEvent = "Data rate recovered: " + msg.str();
          }
          statusInfo.measuredFps = measuredFps;
          if (clockOffset100ns != INT64_MIN)
          {
            statusInfo.clockOffsetMs = float(clockOffset100ns / 1e4);
          }
        }
      }
      if (!rateEvent.empty())
        SystemEventQueue::push("fproc", rateEvent);

      // Timed and requested splits wait for MinimumNewVideo100ns of new video, which also
      // keeps file names, made to the second, apart. Half a frame absorbs capture jitter.
      const uint64_t halfFrame100ns = fps > 0 ? uint64_t(5e6 / fps) : 0;
      const bool enoughNewVideo =
          frameCount > 0 &&
          video_frame->timestamp + halfFrame100ns >= startTs + TimingConstants::MinimumNewVideo100ns;
      bool splitRequested;
      {
        std::lock_guard<std::mutex> lock(splitMutex);
        processedTs = video_frame->timestamp;
        splitRequested = !crossings.empty() && video_frame->timestamp >= *crossings.begin();
      }

      // The video review app may have trouble reading the last video frame when there is not a multiple of gop_size frames
      // Exact cause unknown.
      if (propChange || count == 0 ||
          (enoughNewVideo && frameCount % keyFrameInterval == 0 &&
           ((useEmbeddedTimestamp && video_frame->timestamp >= nextStartTime) || splitRequested)))
      {
        count++;
        if (splitRequested)
        {
          SystemEventQueue::instance().push(
              "info", "Split Requested");
        }
        // A timed or requested split keeps the encoder running when overlapping. A source
        // change, a reconnect or the first file starts a new recording with no overlap.
        const bool overlapSplit = overlap && frameCount > 0 && !propChange;
        if (frameCount > 0 && !overlapSplit)
        {
          // SystemEventQueue::instance().push(
          //     "debug", "Stopping Recorder and writing json file");
          closeRecording();
        }

        const auto ts100ns = video_frame->timestamp;
        startTs = ts100ns;
        // An overlapping file starts, and is named, at its first overlap frame.
        const auto fileStartTs = overlapSplit ? videoRecorder->splitStartTimestamp(ts100ns) : ts100ns;
        {
          std::lock_guard<std::mutex> lock(splitMutex);
          // A long file split for a crossing the new file holds from its start has
          // nothing to review but the overlap the short file repeats. Only the short file
          // is reported, so Video Review does not open the long one first.
          if (overlapSplit && splitRequested && shortEndTs == 0 && *crossings.begin() >= fileStartTs)
          {
            std::lock_guard<std::mutex> s(statusMutex);
            heldFilename = statusInfo.filename;
          }
          // The previous file owns the first half of the overlap.
          ownedFromTs = fileStartTs + (ts100ns - fileStartTs) / 2;
          // A short file closes after MinimumNewVideo100ns, and owns what the next
          // file does not reach back to. Crossings it holds need no more splits.
          shortOwnedUntilTs = splitRequested ? ts100ns + shortOwned100ns(overlap) : 0;
          shortEndTs = splitRequested ? ts100ns + TimingConstants::MinimumNewVideo100ns : 0;
          crossings.erase(crossings.begin(), crossings.lower_bound(shortOwnedUntilTs));
        }
        // Shorter slices cannot split sooner, and a zero length must not divide below.
        const uint64_t sliceDuration100ns =
            std::max<uint64_t>(durationSecs * 10000000, TimingConstants::MinimumNewVideo100ns);
        if (splitRequested)
        {
          // A requested file closes once it has its minimum new video, so Video Review
          // can open it straight away. The timed split that closes it starts a full slice.
          // Allow the same capture jitter as enoughNewVideo.
          nextStartTime = ts100ns + TimingConstants::MinimumNewVideo100ns - halfFrame100ns;
        }
        else if (count == 1)
        {
          // Starting a recording (or after the source reconnects) starts a full slice
          // from the new file's first frame.
          nextStartTime = ts100ns + sliceDuration100ns;
        }
        else if (ts100ns >= nextStartTime)
        {
          // Keep subsequent slices on the schedule established by the last reset.
          const uint64_t elapsedPeriods = (ts100ns - nextStartTime) / sliceDuration100ns;
          nextStartTime += (1 + elapsedPeriods) * sliceDuration100ns;
        }

        // Construct a filename with the time embedded
        const auto milli = (5000 + fileStartTs) / 10000;

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
        {
          std::lock_guard<std::mutex> s(statusMutex);
          if (filename == statusInfo.filename)
          {
            filename += "_" + std::to_string(count);
          }
        }
        // A source whose clock moved backward restarts recording at an earlier
        // time, which can match an existing recording; never overwrite one.
        {
          const std::string base = filename;
          for (int suffix = 2; std::ifstream(directory + "/" + filename + ".mp4").good() ||
                               std::ifstream(directory + "/" + filename + ".json").good();
               ++suffix)
          {
            filename = base + "_" + std::to_string(suffix);
          }
        }
        jsonFilename = directory + "/" + filename + ".json";
        {
          std::lock_guard<std::mutex> s(statusMutex);
          statusInfo.filename = filename;
          statusInfo.fps = fps;
          statusInfo.width = video_frame->xres;
          statusInfo.height = video_frame->yres;
        }

        pxCropArea.x = std::round((cropArea.x * (video_frame->xres) / 4)) * 4;
        int cropWidth = cropArea.width * video_frame->xres;
        cropWidth = std::min(video_frame->xres - pxCropArea.x, cropWidth);
        pxCropArea.width = int(cropWidth / 4) * 4; // trunc to mult of 4

        pxCropArea.y = std::round((cropArea.y * (video_frame->yres) / 4)) * 4;
        int cropHeight = cropArea.height * video_frame->yres;
        cropHeight = std::min(video_frame->yres - pxCropArea.y, cropHeight);
        pxCropArea.height = int(cropHeight / 4) * 4; // trunc to mult of 4

        // std::cout << "cropArea: " << cropArea.x << "," << cropArea.y << ","
        //           << cropArea.width << "," << cropArea.height << std::endl;

        // std::cout << "pxCropArea: " << pxCropArea.x << "," << pxCropArea.y
        //           << "," << pxCropArea.width << "," << pxCropArea.height
        //           << std::endl;

        {
          SystemEventQueue::instance().push(
              "info", "Opening file " + filename + " in " + directory);
        }

        const auto err =
            overlapSplit
                ? videoRecorder->splitVideoStream(directory, filename, video_frame->timestamp)
                : videoRecorder->openVideoStream(
                      directory, filename,
                      pxCropArea.width ? pxCropArea.width : video_frame->xres,
                      pxCropArea.height ? pxCropArea.height : video_frame->yres, fps,
                      video_frame->timestamp);
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

      auto err = videoRecorder->writeVideoFrame(cropped);
      if (!err.empty())
      {
        // frameCount still holds the frames written, for the sidecar.
        errorMessage = err;
        running = false;
        break;
      }
      lastTs = video_frame->timestamp;
      frameCount++;

      lock.lock();
      if (frameQueue.size() > 500)
      {
        SystemEventQueue::instance().push(
            "fproc", "Error: Frame queue overflow, discarding frames");
        frameQueue = std::queue<FramePtr>();
      }
    }
  }

  // Always stop the recorder, even when an error left frameCount at 0: a failed split still
  // has the previous file open, and a failed open has a partly opened encoder. With no
  // frames no sidecar is written, and stopping a stopped recorder does nothing.
  closeRecording();
  videoRecorder->onFileClosed = nullptr;
  // The file after a held one failed to close; report the held one on its own.
  if (!heldClosedFile.second.empty())
  {
    reportFileClosed(heldClosedFile.first, heldClosedFile.second);
  }
}
