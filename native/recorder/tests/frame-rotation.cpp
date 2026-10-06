#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include "VideoUtils.hpp"
extern "C" {
#include <libswscale/swscale.h>
}

// Reference: swscale UYVY -> I420, then rotate each plane pixel by pixel.
static FramePtr referenceRotate(const FramePtr &source, int rotation) {
  const int w = source->xres, h = source->yres;
  Frame upright(w, h, Frame::YUV420P);
  uint8_t *planes[4] = {upright.data, upright.data + w * h,
                        upright.data + w * h * 5 / 4, nullptr};
  int strides[4] = {w, w / 2, w / 2, 0};
  const uint8_t *src[4] = {source->data, nullptr, nullptr, nullptr};
  int srcStrides[4] = {source->stride, 0, 0, 0};
  SwsContext *sws = sws_getContext(w, h, AV_PIX_FMT_UYVY422, w, h, AV_PIX_FMT_YUV420P,
                                   SWS_BILINEAR | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
  sws_scale(sws, src, srcStrides, 0, h, planes, strides);
  sws_freeContext(sws);

  const bool quarter = rotation == 90 || rotation == -90;
  auto out = std::make_shared<Frame>(quarter ? h : w, quarter ? w : h, Frame::YUV420P);
  uint8_t *outPlanes[3] = {out->data, out->data + w * h, out->data + w * h * 5 / 4};
  for (int p = 0; p < 3; ++p) {
    const int pw = p ? w / 2 : w, ph = p ? h / 2 : h;
    const int ow = quarter ? ph : pw;
    for (int y = 0; y < ph; ++y)
      for (int x = 0; x < pw; ++x) {
        int dx, dy;
        if (rotation == 90) { dx = ph - 1 - y; dy = x; }
        else if (rotation == -90) { dx = y; dy = pw - 1 - x; }
        else { dx = pw - 1 - x; dy = ph - 1 - y; }
        outPlanes[p][dy * ow + dx] = planes[p][y * pw + x];
      }
  }
  return out;
}

static FramePtr makeUyvy(int w, int h, unsigned seed) {
  // Padded stride, like NDI line_stride_in_bytes can be.
  auto frame = std::make_shared<Frame>();
  frame->xres = w; frame->yres = h; frame->stride = w * 2 + 16;
  frame->pixelFormat = Frame::UYVY422;
  frame->data = new uint8_t[frame->stride * h];
  frame->ownData = true;
  std::mt19937 random(seed);
  for (int i = 0; i < frame->stride * h; ++i) frame->data[i] = 16 + random() % 224;
  frame->timestamp = 1234; frame->rotation = 0;
  return frame;
}

static int checkUyvy(int w, int h, int rotation) {
  auto source = makeUyvy(w, h, w * 31 + h);
  auto fused = rotateUyvyToI420(source, rotation);
  auto expected = referenceRotate(source, rotation);
  if (!fused || fused->pixelFormat != Frame::YUV420P ||
      fused->xres != expected->xres || fused->yres != expected->yres) return 10;
  if (fused->timestamp != 1234 || fused->sensorXres != w || fused->sensorYres != h) return 11;
  if (fused->rotation != (rotation == 180 ? -180 : rotation)) return 12;
  const int lumaSize = w * h;
  if (std::memcmp(fused->data, expected->data, lumaSize)) return 13;
  for (int i = lumaSize; i < lumaSize * 3 / 2; ++i)
    if (std::abs(fused->data[i] - expected->data[i]) > 1) return 14;
  return 0;
}

static int checkI420(int w, int h, bool clockwise) {
  auto source = std::make_shared<Frame>(w, h, Frame::YUV420P);
  std::mt19937 random(w + h);
  for (int i = 0; i < w * h * 3 / 2; ++i) source->data[i] = random();
  auto rotated = rotateFrame90(source, clockwise);
  // Four quarter turns in the same direction must give back the original.
  for (int i = 0; i < 3; ++i) rotated = rotateFrame90(rotated, clockwise);
  return std::memcmp(rotated->data, source->data, w * h * 3 / 2) ? 20 : 0;
}

template <typename F>
static double millisPerFrame(F work) {
  const int iterations = 20;
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < iterations; ++i) work();
  std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
  return elapsed.count() / iterations;
}

static void benchmark(int w, int h) {
  auto source = makeUyvy(w, h, 7);
  Frame converted(h, w, Frame::YUV420P);
  uint8_t *planes[4] = {converted.data, converted.data + w * h,
                        converted.data + w * h * 5 / 4, nullptr};
  int strides[4] = {h, h / 2, h / 2, 0};
  SwsContext *sws = sws_getContext(h, w, AV_PIX_FMT_UYVY422, h, w, AV_PIX_FMT_YUV420P,
                                   SWS_BICUBIC, nullptr, nullptr, nullptr);
  // Previous path: UYVY rotate, then the encoder's UYVY -> I420 sws_scale.
  const double before = millisPerFrame([&] {
    auto rotated = rotateFrame90(source, true);
    const uint8_t *src[4] = {rotated->data, nullptr, nullptr, nullptr};
    int srcStrides[4] = {rotated->stride, 0, 0, 0};
    sws_scale(sws, src, srcStrides, 0, w, planes, strides);
  });
  sws_freeContext(sws);
  const double after = millisPerFrame([&] { rotateUyvyToI420(source, 90); });
  std::cout << w << "x" << h << " rotate+convert: before=" << before
            << "ms after=" << after << "ms (" << before / after << "x)" << std::endl;
}

int main(int argc, char **argv) {
  // Sizes that are and are not multiples of the 32px rotation tile.
  const int sizes[][2] = {{64, 32}, {70, 46}, {2, 2}, {1920, 1080}};
  for (auto &size : sizes) {
    for (int rotation : {90, -90, -180, 180}) {
      if (int err = checkUyvy(size[0], size[1], rotation)) {
        std::cerr << "UYVY " << size[0] << "x" << size[1] << " rotation=" << rotation
                  << " failed: " << err << std::endl;
        return err;
      }
    }
    for (bool clockwise : {true, false}) {
      if (int err = checkI420(size[0], size[1], clockwise)) {
        std::cerr << "I420 " << size[0] << "x" << size[1] << " failed" << std::endl;
        return err;
      }
    }
  }
  if (rotateUyvyToI420(makeUyvy(3, 2, 1), 90) || rotateUyvyToI420(makeUyvy(4, 2, 1), 45))
    return 30;
  std::cout << "rotation checks passed" << std::endl;
  if (argc > 1 && std::string(argv[1]) == "--benchmark") {
    benchmark(1920, 1080);
    benchmark(3840, 2160);
  }
}
