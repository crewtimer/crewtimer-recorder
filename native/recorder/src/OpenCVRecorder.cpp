#include "VideoRecorder.hpp"
#include <opencv2/opencv.hpp>

class OpenCVRecorder : public VideoRecorder {
  bool active = false;
  cv::VideoWriter video_writer;
  cv::Mat frame;
  std::string outputFile;
  std::string tmpFile;

public:
  int openVideoStream(std::string directory, std::string filename, int width,
                      int height, float fps) {
    outputFile = filename + ".mp4";
    tmpFile = directory + "/" + "tmp-" + outputFile;
    outputFile = directory + "/" + outputFile;
    // std::cout << "Creating file '" << outputFile << "'" << std::endl;
    this->video_writer =
        cv::VideoWriter(tmpFile.c_str(),
#if 0
        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps,
        cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, // 180% 8MB/Min
        cv::VideoWriter::fourcc('X', '2', '6', '4'), fps, // fails
        cv::VideoWriter::fourcc('H', '2', '6', '4'), fps, // out.mkv 220% 10MB/Min
        cv::VideoWriter::fourcc('D', 'I', 'V', 'X'), fps, // out.avi 60% 18MB/Min
        cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, // out.mp4 60% 18MB/Min
#endif
                        cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                        fps, // out.mp4 60% 18MB/Min
                        cv::Size(width, height));
    active = true;
    return 0;
  }
  int writeVideoFrame(FramePtr video_frame) {

    switch (video_frame->pixelFormat) {

    case Frame::PixelFormat::RGBX: {
      cv::Mat ndi_frame(cv::Size(video_frame->xres, video_frame->yres), CV_8UC4,
                        video_frame->data, cv::Mat::AUTO_STEP);
      cv::cvtColor(ndi_frame, this->frame, cv::COLOR_RGBA2BGR);
      this->video_writer.write(this->frame);
      break;
    }
    case Frame::PixelFormat::BGR: {
      cv::Mat ndi_frame(cv::Size(video_frame->xres, video_frame->yres), CV_8UC3,
                        video_frame->data, cv::Mat::AUTO_STEP);
      this->video_writer.write(ndi_frame);
      break;
    }

    case Frame::PixelFormat::UYVY422:
    default: {
      // UYVY422 format, where each pixel consists
      // of two chrominance (U and V) values and two
      // luminance (Y) values.
      cv::Mat ndi_frame(cv::Size(video_frame->xres, video_frame->yres), CV_8UC2,
                        video_frame->data, cv::Mat::AUTO_STEP);

      cv::cvtColor(ndi_frame, this->frame, cv::COLOR_YUV2BGR_Y422);
      this->video_writer.write(this->frame);
      break;
    }
    }

    return 0;
  }
  int stop() {
    if (active) {
      this->video_writer.release();
      this->frame.release();
      active = false;
    }
    // Attempt to rename the file
    if (std::rename(tmpFile.c_str(), outputFile.c_str()) == 0) {
      // std::cout << "File successfully renamed from " << tmpFile << " to "
      //           << outputFile << std::endl;
    } else {
      // If renaming failed, print an error message
      perror("Error renaming file");
      return 1;
    }
    return 0;
  }
  ~OpenCVRecorder() { stop(); }
};

std::shared_ptr<VideoRecorder> createOpenCVRecorder() {
  return std::shared_ptr<OpenCVRecorder>(new OpenCVRecorder());
}

void testopencv() {
  auto recorder = createOpenCVRecorder();
  recorder->openVideoStream("/tmp", "test", 640, 480, 30);
  const auto image = new uint8_t[640 * 480 * 3];
  for (int pixel = 0; pixel < 640 * 480 * 3; pixel += 3) {
    image[pixel] = pixel > 640 * 480 && pixel < 640 * 480 * 2 ? 255 : 0;
    image[pixel + 1] = pixel > 640 * 480 * 2 ? 255 : 0;
    image[pixel + 2] = 255;
  }

  auto frame = FramePtr(new Frame());
  frame->xres = 640;
  frame->yres = 480;
  frame->timestamp = 0;
  frame->data = image;
  frame->frame_rate_N = 30;
  frame->frame_rate_D = 1;
  frame->pixelFormat = Frame::PixelFormat::BGR;
  for (int i = 0; i < 30; i++) {
    recorder->writeVideoFrame(frame);
  }
  recorder->stop();
}
