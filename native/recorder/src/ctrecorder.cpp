#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// Windows-specific includes and definitions
#include <windows.h>
#else
// Unix-specific includes and definitions

#include <unistd.h> // fork, setsid, XX_FILENO
#endif

#include "VideoController.hpp"
#include "util.hpp"

using namespace std::chrono;

#ifdef _WIN32
void daemonize() {}
#else
void daemonize() {
  pid_t pid;

  // Fork off the parent process
  pid = fork();

  if (pid < 0) {
    exit(EXIT_FAILURE);
  }

  // If we got a good PID, exit the parent process
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  // At this point, we are in the child process

  // Create a new SID for the child process
  if (setsid() < 0) {
    exit(EXIT_FAILURE);
  }

  // Change the current working directory
  // if ((chdir("/")) < 0) {
  //   exit(EXIT_FAILURE);
  // }

  // Close out the standard file descriptors
  close(STDIN_FILENO);
  // close(STDOUT_FILENO);
  // close(STDERR_FILENO);
}
#endif
std::function<void()> stopHandler = []() {};
void signalHandler(int signal) {
  if (signal == SIGINT) {
    std::cout << std::endl << "SIGINT received, shutting down" << std::endl;
    if (stopHandler) {
      std::cout << std::endl << "Calling stop handler" << std::endl;
      stopHandler();
      exit(0);
    } else {
      exit(0);
    }
  }
}

void testopencv();

void testrecorder(std::shared_ptr<VideoRecorder> recorder) {
  std::cout << "testrecorder start" << std::endl;
  recorder->openVideoStream("./", "test", 640, 480, 30);
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

  std::cout << "testrecorder end" << std::endl;
}

int main(int argc, char *argv[]) {
  std::shared_ptr<VideoRecorder> videoRecorder;
  std::signal(SIGINT, signalHandler);

  bool initialConnect = true;
  std::string recorders = "null";
#ifdef USE_FFMPEG
  recorders += " | ffmpeg";
#endif
#ifdef USE_OPENCV

  recorders += " | opencv";
#endif
#ifdef USE_APPLE
  recorders += " | apple";
#endif

  std::map<std::string, std::string> args;

  // Default values for optional parameters
  args["-ndi"] = "";
  args["-encoder"] = "ffmpeg";
  args["-dir"] = ".";
  args["-prefix"] = "CT";
  args["-i"] = "10";
  args["-daemon"] = "false";
  args["-u"] = "false";

  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "-encoder" || arg == "-dir" || arg == "-prefix" ||
         arg == "-i" || arg == "-ndi") &&
        i + 1 < argc) {
      args[arg] =
          argv[++i]; // Increment 'i' to skip next argument since it's a value
    } else if (arg == "-daemon" || arg == "-u") {
      args[arg] = "true";
    } else {
      auto msg = "Unknown or incomplete option: " + arg;
      std::cerr << msg << std::endl;
      return -1;
    }
  }

  // Check for required arguments
  if (args.find("-encoder") == args.end() || args.find("-dir") == args.end() ||
      args.find("-prefix") == args.end() || args.find("-i") == args.end()) {
    auto msg = "Missing required arguments.";
    std::cerr << msg << std::endl;
    return -1;
  }

  if (args["-u"] == "true") {
    std::cout << "Usage: -encoder <" << recorders
              << "> -dir <dir> -prefix "
                 "<prefix> -i <interval secs> -ndi <name> -daemon"
              << std::endl;
    std::cout << "On macos, increase the kernel UDP buffer size: " << std::endl
              << "sudo sysctl -w net.inet.udp.maxdgram=4000000" << std::endl;
    return -1;
  }

  if (args["-daemon"] == "true") {
    std::cout << "Running in unattended mode." << std::endl;
    daemonize();
  }

  const auto directory = args["-dir"];
  const auto prefix = args["-prefix"];
  const auto interval = std::stoi(args["-i"]);
  const auto srcName = args["-ndi"];
  const auto daemon = args["-daemon"] == "true";
  const auto encoder = args["-encoder"];

  auto recorder = std::shared_ptr<VideoController>(
      new VideoController(srcName, encoder, directory, prefix, interval));
  recorder->start();

  auto startShutdown = [recorder]() { recorder->stop(); };
  stopHandler = startShutdown;

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
#if 0
#ifdef USE_APPLE
  if (encoder == "apple") {
    std::cout << "Using Apple VideoToolbox api." << std::endl;
    videoRecorder = createAppleRecorder();
  }
#endif
#ifdef USE_OPENCV
  if (encoder == "opencv") {
    std::cout << "Using opencv recorder." << std::endl;
    videoRecorder = createOpenCVRecorder();
    // default
  }
#endif

#if USE_FFMPEG
  if (encoder == "ffmpeg") {
    std::cout << "Using ffmpeg recorder." << std::endl;
    videoRecorder = createFfmpegRecorder();
  }
#endif

 if (encoder == "null") {
    std::cout << "Using null recorder." << std::endl;
    videoRecorder = createNullRecorder();
  }


  testrecorder(videoRecorder);

  if (!videoRecorder) {
    auto msg = "Unknown encoder type: " + encoder;
    std::cerr << msg << std::endl;
    return msg;
  }

  // "output.mp4"; // cv::VideoWriter::fourcc('m', 'p', '4', 'v')
  // "output.mkv"; //  cv::VideoWriter::fourcc('X', '2', '6', '4')

  std::shared_ptr<FrameProcessor> frameProcessor(
      new FrameProcessor(directory, prefix, videoRecorder, duration));

  std::shared_ptr<MulticastReceiver> mcastListener(
      new MulticastReceiver("239.215.23.42", 52342));
  mcastListener->setMessageCallback([frameProcessor](const json &j) {
    std::cout << "Received JSON: " << j.dump() << std::endl;
    auto command = j.value<std::string>("cmd", "");
    if (command == "split-video") {
      frameProcessor->splitFile();
    }
  });

  mcastListener->start();

  // // We create the NDI sender
  // NDIlib_send_create_t create_params;
  // create_params.clock_video = true;
  // create_params.clock_audio = false;
  // NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&create_params);

  // The current time in 480kHz intervals which allows us to accurately know
  // exactly how many audio samples are needed. This divides into all common
  // video frame-rates.

  // auto reader = createBaslerReader();
  auto reader = createNdiReader(srcName);

  auto startShutdown = [reader, frameProcessor, mcastListener]() {
    std::cout << "Shutting down..." << std::endl;

    std::cout << "Stopping video reader..." << std::endl;
    reader->stop();
    std::cout << "Stopping frame processor..." << std::endl;
    frameProcessor->stop();
    std::cout << "Stopping multicast listener..." << std::endl;
    mcastListener->stop();

    std::cout << "Recording finished" << std::endl;
  };
  stopHandler = startShutdown;

  reader->open(frameProcessor);
  reader->start();
#endif
  // Finished
  return 0;
}
