// Overlapped recording through FrameProcessor and FFRecorder, with the hardware and the
// software encoder. Checks that every file decodes on its own from a keyframe, that a
// frame in two files has the same UTC time and picture in both, that each overlap is at
// least RecordingOverlap100ns and only neighbouring files overlap, that the files cover
// every frame, and that sidecars and file names match the files.
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>
#include "FrameProcessor.hpp"
#include "TimingConstants.hpp"
#include "VideoRecorder.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

static const int W = 640, H = 360, GOP = 12;
static const uint64_t FRAME100NS = 166667; // 60 fps
static int failures = 0;
#define CHECK(cond, msg)                                                   \
  do                                                                       \
  {                                                                        \
    if (!(cond))                                                           \
    {                                                                      \
      std::cout << "  FAIL: " << msg << std::endl;                         \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

static int logErrors = 0;
static void countErrors(void *, int level, const char *fmt, va_list args)
{
  if (level <= AV_LOG_ERROR)
  {
    ++logErrors;
    vfprintf(stderr, fmt, args);
  }
}

// Capture times with jitter and one dropped frame.
static std::vector<uint64_t> captureTimes(uint64_t start, int count)
{
  std::vector<uint64_t> ts;
  for (int i = 0, n = 0; int(ts.size()) < count; ++i)
  {
    if (i == 500)
      continue;
    ts.push_back(start + i * FRAME100NS + (i * 7919 % 41) * 50);
    ++n;
  }
  return ts;
}

static FramePtr makeFrame(int i, uint64_t ts)
{
  auto frame = std::make_shared<Frame>(W, H, Frame::YUV420P);
  uint32_t noise = 977u * i + 1;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
    {
      noise = noise * 1664525u + 1013904223u;
      frame->data[y * W + x] = uint8_t(16 + (x + 2 * y + 5 * i) % 200 + ((noise >> 28) & 7));
    }
  std::fill(frame->data + W * H, frame->data + W * H * 3 / 2, 128);
  frame->timestamp = ts;
  frame->frame_rate_N = 60;
  frame->frame_rate_D = 1;
  return frame;
}

struct Decoded
{
  std::vector<int64_t> utcUs;
  std::vector<uint64_t> hashes;
  bool firstPacketKey = false;
  int errors = 0;
};

static Decoded decode(const std::string &path)
{
  Decoded d;
  logErrors = 0;
  AVFormatContext *ic = nullptr;
  if (avformat_open_input(&ic, path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(ic, nullptr) < 0)
  {
    d.errors = 1;
    return d;
  }
  const AVDictionaryEntry *utc = av_dict_get(ic->metadata, "com.crewtimer.first_utc_us", nullptr, 0);
  const int64_t firstUtc = utc ? std::strtoll(utc->value, nullptr, 10) : 0;
  AVStream *st = ic->streams[0];
  const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
  AVCodecContext *dec = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(dec, st->codecpar);
  avcodec_open2(dec, codec, nullptr);
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  bool firstPacket = true;
  auto drain = [&]()
  {
    while (avcodec_receive_frame(dec, frame) >= 0)
    {
      uint64_t hash = 1469598103934665603ULL;
      for (int y = 0; y < frame->height; ++y)
        for (int x = 0; x < frame->width; ++x)
          hash = (hash ^ frame->data[0][y * frame->linesize[0] + x]) * 1099511628211ULL;
      d.utcUs.push_back(firstUtc + av_rescale_q(frame->best_effort_timestamp, st->time_base, av_make_q(1, 1000000)));
      d.hashes.push_back(hash);
    }
  };
  while (av_read_frame(ic, pkt) >= 0)
  {
    if (firstPacket)
      d.firstPacketKey = pkt->flags & AV_PKT_FLAG_KEY;
    firstPacket = false;
    if (avcodec_send_packet(dec, pkt) < 0)
      ++d.errors;
    av_packet_unref(pkt);
    drain();
  }
  avcodec_send_packet(dec, nullptr);
  drain();
  d.errors += logErrors;
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec);
  avformat_close_input(&ic);
  return d;
}

struct FileInfo
{
  std::string path;
  uint64_t firstTs = 0, lastTs = 0;
  int64_t frames = 0;
  // Sidecars print seconds through a double, which keeps about 16 digits: a few 100 ns.
  uint64_t tolerance100ns = 0;
};

static bool near(uint64_t a, uint64_t b, uint64_t tolerance) { return (a > b ? a - b : b - a) <= tolerance; }

static std::string jsonValue(const std::string &json, const std::string &key)
{
  const size_t at = json.find("\"" + key + "\":");
  if (at == std::string::npos)
    return "";
  size_t begin = json.find_first_not_of(" \"", at + key.size() + 3);
  size_t end = json.find_first_of("\",\n", begin);
  return json.substr(begin, end - begin);
}

// The frames each file should hold follow from the source times: check the files hold
// exactly those, with matching times and pictures where they overlap. Returns the first
// and last source frame of each file.
static std::vector<std::pair<int, int>> verify(const std::vector<FileInfo> &files, const std::vector<uint64_t> &ts,
                                               bool overlapping = true)
{
  const uint64_t t0 = ts[0];
  std::map<int64_t, int> indexOfUs; // expected UTC us of each source frame
  for (size_t i = 0; i < ts.size(); ++i)
    indexOfUs[int64_t((t0 + 5) / 10) + av_rescale_q(ts[i] - t0, av_make_q(1, 10000000), av_make_q(1, 1000000))] = int(i);
  std::map<int, uint64_t> hashOf;
  std::vector<std::pair<int, int>> ranges;
  for (size_t f = 0; f < files.size(); ++f)
  {
    const FileInfo &file = files[f];
    Decoded d = decode(file.path);
    CHECK(d.errors == 0, file.path << " has " << d.errors << " decode errors");
    CHECK(d.firstPacketKey, file.path << " does not start with a keyframe");
    CHECK(int64_t(d.utcUs.size()) == file.frames, file.path << " decoded " << d.utcUs.size() << " of " << file.frames << " frames");
    if (d.utcUs.empty())
      return ranges;
    int first = -1, last = -1;
    for (size_t i = 0; i < d.utcUs.size(); ++i)
    {
      auto it = indexOfUs.find(d.utcUs[i]);
      // Without overlap each file rounds UTC from its own first frame, so may be 1 us off.
      for (int64_t off : {-1, 1})
        if (it == indexOfUs.end() && !overlapping)
          it = indexOfUs.find(d.utcUs[i] + off);
      CHECK(it != indexOfUs.end(), file.path << " frame " << i << " has UTC " << d.utcUs[i] << " us, no source frame");
      if (it == indexOfUs.end())
        return ranges;
      const int index = it->second;
      CHECK(first < 0 || index == last + 1, file.path << " skips from source frame " << last << " to " << index);
      if (first < 0)
        first = index;
      last = index;
      // A frame in two files must decode to the same picture.
      if (hashOf.count(index))
        CHECK(hashOf[index] == d.hashes[i], "source frame " << index << " differs between files");
      hashOf[index] = d.hashes[i];
    }
    CHECK(near(ts[first], file.firstTs, file.tolerance100ns),
          file.path << " first frame " << ts[first] << " but reported " << file.firstTs);
    CHECK(near(ts[last], file.lastTs, file.tolerance100ns),
          file.path << " last frame " << ts[last] << " but reported " << file.lastTs);
    if (f + 1 < files.size())
      CHECK(file.frames % GOP == 0, file.path << " has " << file.frames << " frames, not a multiple of " << GOP);
    ranges.push_back(std::make_pair(first, last));
    std::cout << "  " << file.path.substr(file.path.find_last_of('/') + 1) << ": source frames " << first << "-" << last
              << " (" << file.frames << ")";
    if (f > 0)
      std::cout << ", overlap " << (ts[ranges[f - 1].second + 1] - ts[first]) / 1e7 << " s";
    std::cout << std::endl;
  }
  CHECK(ranges.front().first == 0 && ranges.back().second == int(ts.size()) - 1, "files do not cover every frame");
  for (size_t f = 1; f < ranges.size(); ++f)
  {
    const int liveStart = ranges[f - 1].second + 1;
    if (!overlapping)
    {
      CHECK(ranges[f].first == liveStart, "file " << f << " does not follow on from the previous file");
      continue;
    }
    CHECK(ranges[f].first < liveStart, "file " << f << " does not overlap the previous file");
    const uint64_t overlap = ts[liveStart] - ts[ranges[f].first];
    // An overlap stops at the previous split, which may be less than the full overlap back.
    const bool clamped = f >= 2 && ranges[f].first == ranges[f - 2].second + 1;
    // The recorder allows half a frame of capture jitter.
    if (!clamped)
      CHECK(overlap + FRAME100NS / 2 >= TimingConstants::RecordingOverlap100ns &&
                overlap < TimingConstants::RecordingOverlap100ns + GOP * FRAME100NS,
            "file " << f << " overlap is " << overlap / 1e7 << " s");
    if (f >= 2)
      CHECK(ranges[f].first > ranges[f - 2].second, "file " << f << " overlaps file " << f - 2);
  }
  return ranges;
}

static std::vector<FileInfo> listRecording(const std::string &dir)
{
  std::vector<std::string> names;
  DIR *d = opendir(dir.c_str());
  for (dirent *e; d && (e = readdir(d));)
  {
    const std::string name = e->d_name;
    if (name.size() > 4 && name.substr(name.size() - 4) == ".mp4")
      names.push_back(name.substr(0, name.size() - 4));
  }
  if (d)
    closedir(d);
  std::sort(names.begin(), names.end()); // Video Review orders files by name
  std::vector<FileInfo> files;
  for (const std::string &name : names)
  {
    CHECK(name.compare(0, 4, "tmp-") != 0, "unfinished file " << name);
    std::ifstream json(dir + "/" + name + ".json");
    std::stringstream ss;
    ss << json.rdbuf();
    CHECK(json.good(), name << " has no sidecar");
    FileInfo file;
    file.path = dir + "/" + name + ".mp4";
    file.firstTs = uint64_t(std::llround(std::stod("0" + jsonValue(ss.str(), "startTs")) * 1e7));
    file.lastTs = uint64_t(std::llround(std::stod("0" + jsonValue(ss.str(), "stopTs")) * 1e7));
    file.frames = std::atoll(("0" + jsonValue(ss.str(), "numFrames")).c_str());
    file.tolerance100ns = 5;
    files.push_back(file);
  }
  return files;
}

static void waitForBacklog(FrameProcessor &processor)
{
  while (processor.getStatus().frameBacklog > 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  std::this_thread::sleep_for(std::chrono::milliseconds(50)); // the last frame finishes
}

// Recording through FrameProcessor with sliceSecs slices and a split requested once the
// frames before each of requestAt are processed. liveStarts are the first frames of each
// file after the first that are not in the previous file.
static void testFrameProcessor(const std::string &dir, bool software, bool overlap, int sliceSecs,
                               const std::vector<size_t> &requestAt, const std::vector<int> &liveStarts)
{
  const uint64_t start = 17672256005000000ULL;
  // FrameProcessor applies the global capture-time correction before recording.
  std::vector<uint64_t> recorded = captureTimes(start - TimingConstants::GlobalTimestampCorrection100ns, 840);
  auto recorder = createFfmpegRecorder(80, software, overlap);
  {
    FrameProcessor processor(dir, "OV_", recorder, sliceSecs, FrameProcessor::FRectangle(0, 0, 1, 1),
                             FrameProcessor::Guide(), overlap);
    for (size_t i = 0; i < recorded.size(); ++i)
    {
      if (std::find(requestAt.begin(), requestAt.end(), i) != requestAt.end())
      {
        waitForBacklog(processor);
        processor.splitFile();
      }
      while (processor.getStatus().frameBacklog > 30)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      processor.addFrame(makeFrame(int(i), recorded[i] + TimingConstants::GlobalTimestampCorrection100ns));
    }
    waitForBacklog(processor);
    CHECK(processor.getStatus().error.empty(), "frame processor error: " << processor.getStatus().error);
    processor.stop();
  }
  std::vector<FileInfo> files = listRecording(dir);
  CHECK(files.size() == liveStarts.size() + 1, "expected " << liveStarts.size() + 1 << " files, found " << files.size());
  const std::vector<std::pair<int, int>> ranges = verify(files, recorded, overlap);
  for (size_t f = 1; f < ranges.size() && f <= liveStarts.size(); ++f)
    CHECK(ranges[f - 1].second + 1 == liveStarts[f - 1],
          "file " << f << " new video starts at frame " << ranges[f - 1].second + 1 << ", expected " << liveStarts[f - 1]);
}

// Crossings with 60 s slices, as in the split-on-timestamp diagram. Crossings at 4 s and
// 8 s split long files into short ones that close after 2 s of new video. One at 9.5 s,
// in the last second of a short file, makes one more short file. None is needed for a
// crossing in a short file's first second (4.5 s, 8.33 s), one while a split is waiting
// (9.58 s), one owned by a closed file, or a duplicate split request.
static void testCrossings(const std::string &dir, bool software)
{
  const uint64_t start = 17672256005000000ULL;
  std::vector<uint64_t> recorded = captureTimes(start - TimingConstants::GlobalTimestampCorrection100ns, 840);
  auto recorder = createFfmpegRecorder(80, software, true);
  {
    FrameProcessor processor(dir, "OV_", recorder, 60, FrameProcessor::FRectangle(0, 0, 1, 1),
                             FrameProcessor::Guide(), true);
    for (size_t i = 0; i < recorded.size(); ++i)
    {
      const bool splits = i == 240 || i == 480 || i == 570;
      const bool covered = i == 270 || i == 500 || i == 575;
      if (splits || covered || i == 780)
        waitForBacklog(processor);
      if (splits)
        CHECK(processor.splitAt(recorded[i]), "crossing at frame " << i << " made no split");
      if (covered)
        CHECK(!processor.splitAt(recorded[i]), "crossing at frame " << i << " made an extra split");
      if (i == 240)
        CHECK(!processor.splitFile(), "split request right after a crossing not dropped");
      if (i == 780)
        CHECK(!processor.splitAt(recorded[500]), "crossing in a closed file not dropped");
      while (processor.getStatus().frameBacklog > 30)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      processor.addFrame(makeFrame(int(i), recorded[i] + TimingConstants::GlobalTimestampCorrection100ns));
    }
    waitForBacklog(processor);
    CHECK(processor.getStatus().error.empty(), "frame processor error: " << processor.getStatus().error);
    processor.stop();
  }
  const std::vector<int> liveStarts = {240, 360, 480, 600, 720};
  std::vector<FileInfo> files = listRecording(dir);
  CHECK(files.size() == liveStarts.size() + 1, "expected " << liveStarts.size() + 1 << " files, found " << files.size());
  const std::vector<std::pair<int, int>> ranges = verify(files, recorded);
  for (size_t f = 1; f < ranges.size() && f <= liveStarts.size(); ++f)
    CHECK(ranges[f - 1].second + 1 == liveStarts[f - 1],
          "file " << f << " new video starts at frame " << ranges[f - 1].second + 1 << ", expected " << liveStarts[f - 1]);
}

// Crossings with 60 s slices, each given to splitAt just before its frame is added, as
// an offset from that frame's time. Each must make a split, starting files whose new
// video starts at liveStarts.
static void testCrossingsAt(const std::string &dir, bool software,
                            const std::vector<std::pair<size_t, int64_t>> &crossings,
                            const std::vector<int> &liveStarts)
{
  const uint64_t start = 17672256005000000ULL;
  std::vector<uint64_t> recorded = captureTimes(start - TimingConstants::GlobalTimestampCorrection100ns, 600);
  auto recorder = createFfmpegRecorder(80, software, true);
  {
    FrameProcessor processor(dir, "OV_", recorder, 60, FrameProcessor::FRectangle(0, 0, 1, 1),
                             FrameProcessor::Guide(), true);
    for (size_t i = 0; i < recorded.size(); ++i)
    {
      for (const auto &crossing : crossings)
      {
        if (crossing.first != i)
          continue;
        waitForBacklog(processor);
        CHECK(processor.splitAt(uint64_t(int64_t(recorded[i]) + crossing.second)),
              "crossing given at frame " << i << " made no split");
      }
      while (processor.getStatus().frameBacklog > 30)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      processor.addFrame(makeFrame(int(i), recorded[i] + TimingConstants::GlobalTimestampCorrection100ns));
    }
    waitForBacklog(processor);
    CHECK(processor.getStatus().error.empty(), "frame processor error: " << processor.getStatus().error);
    processor.stop();
  }
  std::vector<FileInfo> files = listRecording(dir);
  CHECK(files.size() == liveStarts.size() + 1, "expected " << liveStarts.size() + 1 << " files, found " << files.size());
  const std::vector<std::pair<int, int>> ranges = verify(files, recorded);
  for (size_t f = 1; f < ranges.size() && f <= liveStarts.size(); ++f)
    CHECK(ranges[f - 1].second + 1 == liveStarts[f - 1],
          "file " << f << " new video starts at frame " << ranges[f - 1].second + 1 << ", expected " << liveStarts[f - 1]);
}

// Splits at the given frames, each a multiple of the keyframe interval, through the
// recorder directly. An overlap stops at the previous split, so starts[i] is the first
// frame expected in the file split at splits[i].
static void testSplits(const std::string &dir, bool software, const std::vector<size_t> &splits,
                       const std::vector<size_t> &starts)
{
  const std::vector<uint64_t> ts = captureTimes(17672256005000000ULL, 600);
  auto recorder = createFfmpegRecorder(80, software, true);
  std::vector<FileInfo> files;
  recorder->onFileClosed = [&](const RecordedFile &f)
  {
    FileInfo file;
    file.path = f.directory + "/" + f.filename + ".mp4";
    file.firstTs = f.firstTs100ns;
    file.lastTs = f.lastTs100ns;
    file.frames = f.frames;
    files.push_back(file);
  };
  std::string err = recorder->openVideoStream(dir, "P0", W, H, 60, ts[0]);
  for (size_t i = 0, next = 0; err.empty() && i < ts.size(); ++i)
  {
    if (next < splits.size() && i == splits[next])
    {
      const uint64_t first = recorder->splitStartTimestamp(ts[i]);
      CHECK(first == ts[starts[next]], "split at " << i << " predicted start " << first);
      err = recorder->splitVideoStream(dir, "P" + std::to_string(i), ts[i]);
      ++next;
    }
    if (err.empty())
      err = recorder->writeVideoFrame(makeFrame(int(i), ts[i]));
  }
  if (err.empty())
    err = recorder->stop();
  CHECK(err.empty(), err);
  CHECK(files.size() == splits.size() + 1, "expected " << splits.size() + 1 << " files, found " << files.size());
  if (files.size() == splits.size() + 1)
    verify(files, ts);
}

// With 4 s slices, the second file cannot be created. Recording stops with an error, and
// the first file must still be finished, renamed, and given its sidecar.
static void testBlockedSplit(const std::string &dir, bool software)
{
  const uint64_t start = 17672256005000000ULL;
  std::vector<uint64_t> recorded = captureTimes(start - TimingConstants::GlobalTimestampCorrection100ns, 360);
  // The split at frame 240 makes a file named from its first overlap frame, 2 s earlier.
  const std::time_t raw = std::time_t((5000 + recorded[120]) / 10000 / 1000);
  char name[64];
  std::strftime(name, sizeof(name), "OV_%Y%m%d_%H%M%S", std::localtime(&raw));
  const std::string block = dir + "/tmp-" + name + ".mp4";
  std::system(("mkdir -p '" + block + "'").c_str()); // a directory cannot be opened as a file
  std::string error;
  auto recorder = createFfmpegRecorder(80, software, true);
  {
    FrameProcessor processor(dir, "OV_", recorder, 4, FrameProcessor::FRectangle(0, 0, 1, 1),
                             FrameProcessor::Guide(), true);
    for (size_t i = 0; i < recorded.size() && processor.getStatus().error.empty(); ++i)
    {
      while (processor.getStatus().frameBacklog > 30 && processor.getStatus().error.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      processor.addFrame(makeFrame(int(i), recorded[i] + TimingConstants::GlobalTimestampCorrection100ns));
    }
    for (int i = 0; i < 400 && processor.getStatus().error.empty(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    error = processor.getStatus().error;
    processor.stop();
  }
  std::system(("rmdir '" + block + "'").c_str());
  CHECK(!error.empty(), "no error for the blocked file");
  std::vector<FileInfo> files = listRecording(dir);
  CHECK(files.size() == 1, "expected 1 file, found " << files.size());
  if (files.size() == 1)
    verify(files, std::vector<uint64_t>(recorded.begin(), recorded.begin() + 240));
}

static std::string makeDir(const std::string &dir)
{
  std::system(("mkdir -p '" + dir + "'").c_str());
  return dir;
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  const std::string root = argv[1];
  av_log_set_callback(countErrors);
  for (bool software : {false, true})
  {
    const std::string mode = software ? "software" : "hardware";
    std::cout << mode << " encoder, 4 s slices:" << std::endl;
    testFrameProcessor(makeDir(root + "/" + mode + "-timed"), software, true, 4, {}, {240, 480, 720});
    // A split waits for 2 s of new video and its file closes after 2 s more; the request
    // at 1.5 s is a duplicate of the one at 1 s.
    std::cout << mode << " encoder, splits requested at 1 s and 1.5 s:" << std::endl;
    testFrameProcessor(makeDir(root + "/" + mode + "-requested"), software, true, 60, {60, 90}, {120, 240});
    // The request at 2.5 s is in the short file the one at 2 s made; the one at 5 s,
    // after it closed, makes another short file.
    std::cout << mode << " encoder, splits requested at 2 s, 2.5 s and 5 s:" << std::endl;
    testFrameProcessor(makeDir(root + "/" + mode + "-requested-again"), software, true, 60, {120, 150, 300},
                       {120, 240, 360, 480});
    // As when the video lags: the crossing splits once the video reaches it, at 6 s,
    // and its short file closes 2 s later.
    std::cout << mode << " encoder, crossing at 6 s while the video is at 4 s:" << std::endl;
    testCrossingsAt(makeDir(root + "/" + mode + "-ahead"), software, {{240, 2 * 10000000LL}}, {360, 480});
    // A crossing an hour ahead, as from another day, waits without hiding the real
    // crossing at 4 s that follows it.
    std::cout << mode << " encoder, crossing an hour ahead, then one at 4 s:" << std::endl;
    testCrossingsAt(makeDir(root + "/" + mode + "-far"), software,
                    {{120, 3600 * 10000000LL}, {240, 0}}, {240, 360});
    std::cout << mode << " encoder, crossings at 4 s, 8 s and 9.5 s:" << std::endl;
    testCrossings(makeDir(root + "/" + mode + "-crossings"), software);
    std::cout << mode << " encoder, 1 s slices:" << std::endl;
    testFrameProcessor(makeDir(root + "/" + mode + "-1s"), software, true, 1, {}, {120, 240, 360, 480, 600, 720});
    // FrameProcessor keeps splits 2 s apart; the recorder must cope with closer ones anyway.
    std::cout << mode << " encoder, recorder splits 1.2 s apart:" << std::endl;
    testSplits(makeDir(root + "/" + mode + "-short"), software, {360, 432}, {240, 360});
    // Faster than libx264's encoder delay, so several files wait for their first packet.
    std::cout << mode << " encoder, recorder splits 0.2 s apart:" << std::endl;
    testSplits(makeDir(root + "/" + mode + "-rapid"), software, {360, 372, 384, 396, 408}, {240, 360, 372, 384, 396});
    std::cout << mode << " encoder, next file cannot be created:" << std::endl;
    testBlockedSplit(makeDir(root + "/" + mode + "-blocked"), software);
  }
  std::cout << "overlap off, 4 s slices:" << std::endl;
  testFrameProcessor(makeDir(root + "/off"), false, false, 4, {}, {240, 480, 720});
  std::cout << "overlap off, splits requested at 1 s and 1.5 s:" << std::endl;
  testFrameProcessor(makeDir(root + "/off-requested"), false, false, 60, {60, 90}, {120, 240});
  std::cout << (failures ? "FAILED" : "PASSED") << std::endl;
  return failures ? 1 : 0;
}
