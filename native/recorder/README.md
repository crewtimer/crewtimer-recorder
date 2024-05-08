# CrewTimer Video Recorder

## Building standalone console recorder

On mac: ```mkdir build && cd build && cmake .. && make```

On windows first build ffmpeg:

```bash
./configure --extra-cflags="-O2 /GS-" --extra-cxxflags="-O2 /GS-" \
    --prefix=../src/ffmpeg-built-win --enable-static --enable-gpl --disable-network \
    --disable-doc --disable-postproc --toolchain=msvc && make && make install
```

Then build the local executable:

```bash
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ..
make
```

## Macos tweaks

Increase kernel udp buffer size: ```sudo sysctl -w net.inet.udp.maxdgram=4000000```

## NDI Cameras

The following cameras support 1920x1080 or better but when streaming 1280x720 works most reliably.

| Price | Zoom    | PTZ | FPS | Weather | Verified | Model                                                                                                                    |
| ----- | ------- | --- | --- | ------- | -------- | ------------------------------------------------------------------------------------------------------------------------ |
| $1450 | 20X     | Yes | 120 |         | Yes      | [AIDA PTZ-NDI3-X20](https://usbroadcast.co/product/aida-imaging-ptz-ndi3-x20b-full-hd-ndihx2-ptz-camera-20x-zoom-black/) |
| $1075 | 30X     |     | 60  |         |          | [AIDA UHD-NDI3-X30](https://usbroadcast.co/product/aida-imaging-uhd-ndihx3-ip-srt-hdmi-poe-30x-zoom-pov-camera/)         |
| $775  | 1X C/CS |     | 120 | IP67    |          | [AIDA HD-NDI3-IP67](https://usbroadcast.co/product/aida-imaging-ndihx3-ip67-weatherproof-pov-camera/)                    |
| $799  | 30X     | Yes | 60  |         | Yes      | [AVKANS NDI PTZ Camera](https://a.co/d/1FIcJW9)                                                                          |
| $719  | 30X     | Yes | 60  |         | Yes      | [SMTAV BX30N](https://www.smtav.com/collections/ndi/products/smtav-ai-tracking-ndi-ptz-camera-30x-optics-zoom)           |
| $669  | 20X     | Yes | 60  |         |          | [SMTAV BX20N](https://www.smtav.com/collections/ndi/products/smtav-20x-optics-zoom-ai-tracking-ndi-ptz-camera-bx20n-w)   |

All cameras needed software upgrades to operate properly.  The SMTAV BA series had unreliable NDI timestamps.
