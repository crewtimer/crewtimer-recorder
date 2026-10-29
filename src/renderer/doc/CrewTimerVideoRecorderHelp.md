# CrewTimer Video Recorder

The CrewTimer Video Recorder app is used to record mp4 video files for use with the [CrewTimer Video Review app](https://admin.crewtimer.com/help/VideoReview).  It provides basic controls for controlling how video is captured, started, and stopped.

For more help beyond this document, visit the [CrewTimer Video Recorder website](https://admin.crewtimer.com/help/VideoRecorder).

## Requirements

* SRT or [NDI](https://en.wikipedia.org/wiki/Network_Device_Interface) capable video camera. **SRT is preferred; configure the camera's SRT stream to use port 1600.** Both protocols provide video over a computer network.
* Hardwired network connection between the recording computer and the camera.
* Internet access, either hardwired or via a hotspot, for camera synchronization with Network Time Protocol (NTP) servers to timestamp the video. If the video will only be used for finish order then Internet access is not required.
* MacOS or Windows computer

## Getting Started

1. Connect your camera to a network switch also connected to your recording computer.  A PoE+ switch to provide power over the ethernet cable to the camera is recommended.
2. Start the CrewTimer Video Recorder app.
3. Using the Camera dropdown in the app, identify the IP address of your camera.
4. Connect to your camera - e.g. <http://10.0.1.188> and log in.  Cameras often default to 'admin' as the username and 'admin' as password.
5. Configure your camera to 1080p video - 1920x1080.  Use 60 fps or higher rate for the video stream.  This is often a choice under an NDI configuration menu. 
6. Click the Play button on the video preview area of the app.
7. Review the event log. Look for reported Gaps in the recording.  It is normal to have an initial Gap reported as the video is starting.
8. Use VLC or CrewTimer Video Review to verify the video is being recorded properly.
9. Use the camera web page (see step 4) to control camera aspects such as zoom, focus, aperature, and exposure as needed.
10. Use the camera web interface presets to quickly change configurations.

## Camera Selection

All SRT and NDI capable cameras should work with CrewTimer Video Recorder.  However, some low end cameras provide unreliable frame timing.

For a list of cameras and other useful accessories, visit the [CrewTimer Video Recorder website](https://admin.crewtimer.com/help/VideoRecorder).

### Recording quality

The Recording Quality setting targets image quality and lets the encoder vary the
bitrate with scene complexity. Choose Standard for smaller files, Medium for a
balance, High (the default) for more detail, or Very High for the most detail and larger files. A
change takes effect the next time recording starts. It cannot recover detail
already lost in the camera stream.

Quality settings use the selected encoder's own scale, so results and file sizes
vary across computers. Apple Silicon uses VideoToolbox quality mode; Intel Macs
require a software x264 encoder in the native FFmpeg build. Windows uses NVIDIA
CQ, Intel ICQ, AMD constant quantization, or Media Foundation. Media Foundation
uses the GPU's hardware encoder when available, falling back to Microsoft's
software encoder, and targets 20, 30, 40, or 60 Mbps for Standard, Medium, High,
and Very High.

The saved `recordingQuality` property accepts integers from 1 to 100 (higher is
better). The UI presets are 60, 70, 80, and 90. For x264, NVIDIA, Intel, and AMD, these
map to CRF/CQ/ICQ/QP values of 26, 23, 20, and 17 respectively. Apple VideoToolbox
uses the property directly on its 0–100 quality scale.
These are approximate mappings, not guarantees of equal visual quality.

### Overlapped recording

Each new file also starts with at least the last 2 seconds of the previous file, so a
finish that crosses a file split can be reviewed in either file. The overlapping frames
are identical in both files, with the same timestamps. Files take about 2 seconds more
space each.

Every file holds at least 2 seconds of new video. A split
that comes sooner, from the slice length or a split request, waits until it has. The
file a split request starts closes again after 2 seconds of new video, so Video Review
can open it straight away, and recording carries on in a file of the normal slice
length. A split request from Video Review while the latest video is already in one of
these short files makes no extra file. Stopping the recording ends the last file at
once.

### Regatta times

On the Source tab, enter the Regatta ID and choose a Hint Waypoint to split the
recording whenever a new time arrives at that waypoint. The recorder shows the regatta's title
once it finds the regatta. No regatta PIN is needed.

Each new time makes sure the crossing is in a file that closes within about 2 seconds,
so Video Review can review it straight away. A time in a long file splits it, starting
a short file; recording then goes back to long files. A time that a closed file or a
closing short file already holds needs no split. A time in the last second of a short
file makes one more short file, since the next file would hold it. A time more than a minute
ahead of this PC's clock, such as one from another day, is ignored. Times only split
while recording. Times already entered when the recorder starts
listening do not cause splits. While the recorder is splitting on the regatta's times,
Video Review does not ask for these splits itself. Without a regatta, Start Recording
asks before recording, Video Review's toolbar marks the recorder with "no hint splits",
and Video Review asks for splits as it does for other recorders.

The Hint Waypoint also pairs the recorder with Video Review: set it to the timing hint
waypoint chosen in Video Review. Video Review then sends this recorder its manual split
requests, shows its status, and lists each new file as soon as it is finished. Several
recorders and Video Review apps can share a network, each pair with its own hint
waypoint. Without a regatta, choose Any to accept split requests from every Video
Review. When the regatta does not have the chosen waypoint, or it is Any, the recorder
picks Finish.

The first file of a recording has no overlap, and neither does the file that starts
after the camera reconnects or changes resolution or frame rate. Use a recent version of
CrewTimer Video Review, which recognises overlapping files.
