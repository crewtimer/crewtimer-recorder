# CrewTimer Video Recorder

The CrewTimer Video Recorder app is used to record mp4 video files for use with the CrewTimer Connect Video Review app.  It provides basic controls for controlling how video is captured, started, and stopped.

## Requirements

* [NDI](https://en.wikipedia.org/wiki/Network_Device_Interface ) capable video camera.  Network Device Interface (NDI) is a protocol used to provide low latency video over a 100Mbit or Gigabit computer network.
* Hardwired network connection between the recording computer and the NDI camera.
* Internet access.  Either hardwired or via a hotspot. The NDI cameras have built-in synchronization with Network Time Protocol (NTP) servers to timestamp the video.  If the video will only be used for finish order then Internet access is not required.
* MacOS or Windows

## Installation

Download the installer:

* Windows - pending release
* [MacOS Installer](https://storage.googleapis.com/resources.crewtimer.com/installers/CrewTimer%20Video%20Recorder-1.0.0.dmg)

## Getting Started

1. Connect your NDI camera to a network switch also connected to your recording computer.
2. Start the CrewTimer Video Recorder app.
3. Using the Camera dropdown in the app, identify the IP address of your camera.
4. Connect to your camera - e.g. <http://10.0.1.188> and log in.  Cameras often default to 'admin' as the username and 'admin' as password.
5. Configure your camera to 720p video - 1280x720.  Use 60 fps or higher rate for the NDI video stream.  This is often a choice under an NDI configuration menu.
6. Click the Play button on the video preview area of the app.
7. Review the event log. Look for reported Gaps in the recording.  It is normal to have an initial Gap reported as the video is starting.
8. Use VLC or CrewTimer Connect to verify the video is being recorded properly.
9. Use the camera web page (see step 4) to control camera aspects such as zoom, focus, aperature, and exposure as needed.
10. Use the camera web interface presets to quickly change configurations.

## Internet Connectivity

NDI cameras have a NTP client built-in which sets the camera time from a network time server.  The camera then timestamps each video frame allowing applications to synchronize multiple video streams gathered separately.  For use with CrewTimer, these timestamps are used to accuratly timestamp each frame in the CrewTimer Connect app.  If internet connectivity is not present, the camera will timestamp video based on it's current notion of time.

If the only use for the video is for finish order determination, Internet connectivity is not required.  However, if the intent is to utilize the video as backup timing in additon to quickly determining finish order, an Internet connection is required.

**NOTE: Do not try to set up your Internet on regatta day for the first time.** You should test out your connectivity ahead of time and understand how your network needs to be configured.

### Hardwired Internet available

If local hardwired internet is available, simple ensure the camera network connection has access to the network.  Hardwired Internet can be from a facility LAN drop or it could be from a hotspot with RJ45 cabled network connections.  The recording computer should preferably be on the same network switch as the camera.  See the equipment list below for an example hotspot.

### Only WiFi available

If only WiFi is available, there are several configurations which can work in this situation.

1. Use a WiFi router in client mode to connect to the available WiFi.  The network ports on the WiFi router would the connect to the camera and laptop.  Many WiFi routers support this type of connectivity.  In this scenario the WiFi in the laptop should be disabled or the WiFi should be set as the preferred Internet port (see instructions below).
2. Utilize Windows Internet Connection Sharing (ICS) to share the WiFi connection with the computer wired interface.  In this scenario, connect a switch to the computer and also the camera.  The camera will obtain an IP address from the computer's ICS software and access the internet via it's WiFi connection.
3. Add your own NTP server on the camera network using a Raspberry Pi.  Use fixed IP addresses on the wired network with the camera NTP server configured to utilize your local NTP server.  The laptop will also be configured with a fixed IP address on the wired network so it can communicate with the camera and record video.  WiFi should be set as the preferred Internet port (see instructions below).

## Camera Selection

All NDI capable cameras should work with CrewTimer Video Recorder.  However, some low end SMTAV cameras provide unreliable frame timing.  The following cameras have been tested and work well with CrewTimer Video Recorder.

| Camera                                                                                              | Cost  | Features                     |
| --------------------------------------------------------------------------------------------------- | ----- | ---------------------------- |
| [SMTAV BX30N Amazon](https://bit.ly/3QS9Ynh)                                                        | $719  | 60 fps, PoE+, PTZ, 30x zoom  |
| [AIDA UHD-NDI3-X30 Amazon](https://amzn.to/3wH9hq3) [Vendor](https://aidaimaging.com/uhd-ndi3-x30/) | $1075 | 60 fps, PoE+, 30x zoom       |
| [AIDA PTZ-NDI3-X20](https://aidaimaging.com/ptz-ndi3-x20/)                                          | $1450 | 120 fps, PoE+, PTZ, 20x zoom |

The SMTAV BX30N camera requires a software update to version 8.02.88 or better to properly work with DHCP.  Contact <info@smtav.com> for firmware updates.  They also have a .pkg file update to fix an issue with the Chrome browser on version 8.02.88.

## Other useful equipment

### Camera Mounting

| Item                                             | Description                                         |
| ------------------------------------------------ | --------------------------------------------------- |
| [NEEWER GM38](https://amzn.to/3K7IPJj)           | Three way geared tripod head.                       |
| [Manfroto Super Clamp](hhttps://amzn.to/4bNIYO1) | Handy to mount your camera to scaffolding or rails. |
| [SLIK Pro 700 DX](https://amzn.to/3QWb7dB)       | Sturdy Tripod.                                      |
| [SLIK Pro CF-734](https://amzn.to/4amuPWU)       | Carbon Fiber Tripod.  Can fit in carry-on luggage   |
| [Focusing Rail](https://amzn.to/3wIi37i)         | Horizontal adjustment of camera on tripod head.     |

### Networking and Power

| Item                                          | Description                                          |
| --------------------------------------------- | ---------------------------------------------------- |
| [BVTech PoE+ Switch](https://amzn.to/3QSzwkt) | Network switch to power camera over CAT5/6 cable.    |
| [LTE Router](https://amzn.to/3QMPTP9)         | Provide local DHCP and Internet access               |
| [Micro USB PoE+](https://amzn.to/3QSqzHw)     | Power micro usb device (e.g. router) via PoE+ switch |
| [USB C PoE+](https://amzn.to/3QU2Vup)         | Power USB C device (e.g. router) via PoE+ switch     |

## Software Tools

NDI Tools are available to help monitor test NDI video streams.  The following programs are recommended and are part of the FREE [NDI Tools](https://ndi.video/tools/) download.  Registration is required.

* [NDI Video Monitor](https://ndi.video/tools/ndi-video-monitor/) - Watch the video stream from the camera from any other device.  Display on a large monitor if desired.
* [NDI Scan Converter](https://ndi.video/tools/ndi-scan-converter/) - Play a youtube video of a regatta, capture the video with NDI Scan Converter as an NDI stream and record it with CrewTimer Recorder to test it out.
* [NDI Test Patterns](https://ndi.video/tools/ndi-test-patterns/) - No camera yet?  Generate your own video stream with test patterns.

## Setting network priority

**Note: This option is only used when you have a local NTP server (e.g. Raspberry Pi) on the wired network without Internet connection.**

When computers have both a wired connection and a WiFi connection, they will typically prioritize traffic to the wired connection.  If the wired network does not have Internet connectivity, it can cause the appearance no Internet capability because it tries to use the hardwired connection instead of WiFi.   This priority selection can be modified so that WiFi is utilized for Internet access in preference to a wired connection.When using laptops with a local network for cameras and a WiFi network for Internet the computer must be configured so the default route is via WiFi.

### MacOS

Go to Network Settings where it shows both the wired and wireless network on the same page.  At the very bottom of the page is a tiny dropdown where you can 'Set Service Order'.  Drag the WiFi connection to be above the wired connection.

### Windows

* Option 1 - Gui

```txt
Network & Internet ->Advanced -> More Network Adapter Options
Select WiFi Properties -> IPV4 -> Properties -> Advanced
Set Interface Metric to 10
```

* Option 2 - Command Line:

Windows-X and select ‘Terminal (Admin)’

```bash
Get-NetIPInterface and note the ifIndex of WiFi.  e.g. 20
Set-NetIPInterface -InterfaceIndex 20 -InterfaceMetric 10
```
