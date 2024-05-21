# CrewTimer Video Recorder

The CrewTimer Video Recorder app is used to record mp4 video files for use with the [CrewTimer Video Review app](https://admin.crewtimer.com/help/VideoReview).  It provides basic controls for controlling how video is captured, started, and stopped.

For more help beyond this document, visit the [CrewTimer Video Recorder website](https://admin.crewtimer.com/help/VideoRecorder).

## Requirements

* [NDI](https://en.wikipedia.org/wiki/Network_Device_Interface ) capable video camera.  Network Device Interface (NDI) is a protocol used to provide low latency video over a 100Mbit or Gigabit computer network.
* Hardwired network connection between the recording computer and the NDI camera.
* Internet access.  Either hardwired or via a hotspot. The NDI cameras have built-in synchronization with Network Time Protocol (NTP) servers to timestamp the video.  If the video will only be used for finish order then Internet access is not required.
* MacOS or Windows computer

## Getting Started

1. Connect your NDI camera to a network switch also connected to your recording computer.  A PoE+ switch to provide power over the ethernet cable to the camera is recommended.
2. Start the CrewTimer Video Recorder app.
3. Using the Camera dropdown in the app, identify the IP address of your camera.
4. Connect to your camera - e.g. <http://10.0.1.188> and log in.  Cameras often default to 'admin' as the username and 'admin' as password.
5. Configure your camera to 720p video - 1280x720.  Use 60 fps or higher rate for the NDI video stream.  This is often a choice under an NDI configuration menu.
6. Click the Play button on the video preview area of the app.
7. Review the event log. Look for reported Gaps in the recording.  It is normal to have an initial Gap reported as the video is starting.
8. Use VLC or CrewTimer Connect to verify the video is being recorded properly.
9. Use the camera web page (see step 4) to control camera aspects such as zoom, focus, aperature, and exposure as needed.
10. Use the camera web interface presets to quickly change configurations.

## Camera Selection

All NDI capable cameras should work with CrewTimer Video Recorder.  However, some low end cameras provide unreliable frame timing.

For a list of cameras and other useful accessories, visit the [CrewTimer Video Recorder website](https://admin.crewtimer.com/help/VideoRecorder).
