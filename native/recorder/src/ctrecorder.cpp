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
bool running;
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
      std::cout << std::endl << "Exiting program" << std::endl;
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

std::shared_ptr<VideoController> recorder;
int main(int argc, char *argv[]) {
  std::shared_ptr<VideoRecorder> videoRecorder;
  std::signal(SIGINT, signalHandler);

  std::string recorders = "null";

  std::map<std::string, std::string> args;

  // Default values for optional parameters
  args["-ndi"] = "";
  args["-dir"] = ".";
  args["-prefix"] = "CT";
  args["-i"] = "10";
  args["-daemon"] = "false";
  args["-u"] = "false";
  args["-timeout"] = "0";

  std::string defaultRecorder = "null";
#ifdef USE_FFMPEG
  recorders += " | ffmpeg";
  if (defaultRecorder == "null") {
    defaultRecorder = "ffmpeg";
  }
#endif
#ifdef USE_OPENCV
  if (defaultRecorder == "null") {
    defaultRecorder = "opencv";
  }
  recorders += " | opencv";
#endif
#ifdef USE_APPLE
  if (defaultRecorder == "null") {
    defaultRecorder = "apple";
  }
  recorders += " | apple";
#endif

  args["-encoder"] = defaultRecorder;

  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "-encoder" || arg == "-dir" || arg == "-prefix" ||
         arg == "-i" || arg == "-ndi" || arg == "-timeout") &&
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
                 "<prefix> -i <interval secs> -ndi <name> -timeout <secs>"
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
  const auto timeout = std::stoi(args["-timeout"]);

  recorder = std::shared_ptr<VideoController>(new VideoController("ndi"));
  recorder->start(srcName, encoder, directory, prefix, interval,
                  {1280 / 4, 720 / 4, 1280 / 2, 720 / 2});

  auto startShutdown = []() {
    recorder->stop();
    recorder = nullptr;
    running = false;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  };
  stopHandler = startShutdown;

  running = true;

  int count = 0;
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    if (timeout != 0) {
      count += 1;
      if (count >= timeout) {
        startShutdown();
        break;
      }
    }
  }

  std::cout << "Main thread exiting." << std::endl;
  // Finished
  return 0;
}
