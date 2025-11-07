#include "VideoRecorder.hpp"

class NullRecorder : public VideoRecorder
{

public:
  std::string openVideoStream(std::string directory, std::string filename,
                              int width, int height, float fps, uint64_t timestamp)
  {
    return "";
  }
  std::string writeVideoFrame(FramePtr video_frame) { return ""; }
  std::string stop() { return ""; }
  ~NullRecorder() { stop(); }
};

std::shared_ptr<VideoRecorder> createNullRecorder()
{
  return std::shared_ptr<NullRecorder>(new NullRecorder());
}
