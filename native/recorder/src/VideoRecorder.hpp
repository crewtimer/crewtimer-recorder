#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "VideoUtils.hpp"

// A file an overlapped recording has finished writing.
struct RecordedFile
{
  std::string directory;
  std::string filename; // without extension
  uint64_t firstTs100ns = 0;
  uint64_t lastTs100ns = 0;
  int64_t frames = 0;
};

class VideoRecorder
{
public:
  virtual std::string openVideoStream(std::string directory,
                                      std::string filename, int width,
                                      int height, float fps, uint64_t timestamp) = 0;
  virtual std::string writeVideoFrame(FramePtr frame) = 0;
  virtual std::string stop() = 0;
  virtual int getKeyFrameInterval()
  {
    return 12;
  }

  // Overlapped recording keeps one encoder open across files and starts each new file
  // with the previous file's last TimingConstants::RecordingOverlap100ns of frames.
  virtual bool supportsSplit() { return false; }
  // Capture time of the first frame a split at splitTs would put in the new file.
  virtual uint64_t splitStartTimestamp(uint64_t splitTs) { return splitTs; }
  // Start a new file. Its live part begins with the next frame written, at splitTs.
  virtual std::string splitVideoStream(std::string directory, std::string filename,
                                       uint64_t splitTs)
  {
    return "Error: Recorder cannot split files";
  }
  // Called on the encoding thread when a split recording has finished a file, before
  // it renames the file into place, so a sidecar written here is there first. Returning
  // true holds the file back: it is renamed into place just after the next file, and is
  // not reported then, or renamed and reported on stop if no next file is placed.
  std::function<bool(const RecordedFile &)> onFileFinished;
  // Called after a split recording renames a finished file into place.
  std::function<void(const RecordedFile &)> onFileClosed;

  virtual ~VideoRecorder() {}
};

std::shared_ptr<VideoRecorder> createOpenCVRecorder();
std::shared_ptr<VideoRecorder> createAppleRecorder();
// forceSoftware skips GPU encoders, for comparing their output with a software encoder.
// overlap enables splitVideoStream.
std::shared_ptr<VideoRecorder> createFfmpegRecorder(int recordingQuality = 80, bool forceSoftware = false,
                                                    bool overlap = false);
std::shared_ptr<VideoRecorder> createNullRecorder();
void testopencv();
