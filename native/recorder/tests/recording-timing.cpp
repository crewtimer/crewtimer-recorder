#include "VideoRecorder.hpp"
#include "TimingConstants.hpp"

#include <algorithm>
#include <cassert>
#include <string>

// Exercise the shared recording path with corrected NDI/SRT-style timestamps.
int main(int argc, char **argv)
{
  assert(argc == 2);
  auto recorder = createFfmpegRecorder();
  const uint64_t start = 17348931041234560ULL -
                         TimingConstants::GlobalTimestampCorrection100ns;
  for (const auto &name : {"ndi", "srt"})
  {
    assert(recorder->openVideoStream(argv[1], name, 64, 64, 59.94f, start).empty());
    for (const uint64_t offset : {0ULL, 166833ULL, 333667ULL, 10010000ULL, 10176833ULL})
    {
      auto frame = std::make_shared<Frame>(64, 64, Frame::YUV420P);
      std::fill(frame->data, frame->data + 64 * 64 * 3 / 2, 128);
      frame->timestamp = start + offset;
      assert(recorder->writeVideoFrame(frame).empty());
    }
    auto invalid = std::make_shared<Frame>(64, 64, Frame::YUV420P);
    invalid->timestamp = start + 10176833ULL;
    assert(!recorder->writeVideoFrame(invalid).empty()); // duplicate
    invalid->timestamp = start - 1;
    assert(!recorder->writeVideoFrame(invalid).empty()); // backwards
    assert(recorder->stop().empty());
  }
}
