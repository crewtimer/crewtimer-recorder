| Version | What's New |
| --- | --- |
| 2.0.4 | Frame timing warnings |
| | For SRT cameras, the log warns when the time between frames is 2 ms or more off the camera's frame rate, during preview as well as recording. The first is logged at once, then at most one summary a minute |
| 2.0.3 | Zoom, focus and update check fixes |
| | Zooming on a point outside the crop region zooms at that point instead of into the crop |
| | The timing line can be dragged while zoomed in: press on the line itself and drag |
| | The horizon line can be dragged up and down as well as placed with a click |
| | Distance focus and the focus slider move straight to the focus position instead of overshooting it; a position the lens can't reach at the current zoom stops at the nearest end |
| | Focus + and − step 2 units at a time where the focus range at the current zoom is known |
| | Calibrate Zoom for the AIDA UHD-NDI3-X30: a guided routine finds the column the lens zooms around, so a target on the timing line stays on it as you zoom. The column is saved for each camera, and Set line puts the timing line back on it |
| | A Search button beside the camera list on the Source tab finds cameras added while another camera is previewing |
| | Beta installs are offered the official release once it ships, and version numbers such as 1.1.1 compare correctly |
| 2.0.2 | Correct video status while the recorder window is hidden |
| 2.0.1 | Overlapped recording and splitting on regatta times |
| | Overlapped recording: each file starts with the last 2 seconds of the one before, so a finish at a split can be reviewed in either file |
| | Split on regatta times: enter a Regatta ID and Hint Waypoint on the Source tab, and each new time makes a short file Video Review can open within about 2 seconds |
| | Start Recording asks before recording without a regatta |
| | Video Review lists each new file as soon as it is finished |
| | Files close faster on USB disks |
| | Camera presets save the crop region and timing line |
| | Timing line colours and a sharper preview on high resolution displays |
| | Focus assist moved into the Focus group; rotation moved to the recorder settings |
| | NVIDIA hardware encoding on Windows |
| | Distance focus for the AIDA UHD-NDI3-X30: enter a distance of 10 to 100 m and focus follows the zoom |
| 1.0.27 | Recorder health monitoring and more reliable recording |
| | Monitor recorder health: warn on low frame rate and show disk usage, warning at 85% full |
| | Broadcast recorder status, warnings and errors to other CrewTimer apps on the network |
| | A split request starts recording if the recorder is stopped |
| | More reliable SRT recording through timestamp jumps and reconnects |
| | Improved camera discovery and connection status |
| | Report every frame gap and classify errors and warnings consistently |
| | Keep native log output and add an Open log folder button |
| | Run alongside CrewTimer Video Review on the same Mac |
| | Update button in the new version dialog |
| 1.0.26 | Major rework of user interface, status messages, and error messages |
| 1.0.25 | Reset recording slice when manual split requested, recording quality improved |
| 1.0.24 | Add warnings and hints for networking issues |
| 1.0.23 | Improve support for portrait orientation video |
| 1.0.22 | Prompt for LocalNetwork access on macos |
| 1.0.21 | Allow screen rotation and shrinking viewport to just cropped video |
| 1.0.20 | Utilize hardware accelerated video encode/decode |
| 1.0.19 | Better MacOS permission prompts |
| 1.0.18 | Support for SRT (Secure Reliable Transport) protocol |
| 1.0.16-17 | Assist with identifying Camera IP Address |
| 1.0.15 | Add icon to open camera web page<br>Add focus assistance metric<br>Support multiple recorders on the network (Start, Finish, etc) |
