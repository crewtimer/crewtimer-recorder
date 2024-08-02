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

## Making a new prebuilt on windows

First, ensure that ffmpeg has been built by following the prior section instructions.

Open a shell via C:\cygwin64\Cygwin.bat.  Alternatively, open a Visual Studio 2022 x64 Native Tools Command Prompt.

```bash
cd c:/Users/glenne/git/crewtimer-recorder/native/recorder
rm -rf node_modules build # start from scratch
rm yarn.lock # removes an error about stringWidth libraries
yarn install # The final step of the install will fail where it tries to get prebuilt binaries.  We'll build our own next
yarn prebuild # This will likely fail on the final step where it tries to upload to github releases
```

If you get an error about a stringWidth require, do the following: `rm -rf node_modules yarn.lock && yarn install`.  A conflict exists between two string packages and an install without a yarn.lock will succeed.

The result is placed into a file such as prebuilds/crewtimer_recorder-v1.0.2-napi-v6-win32-x64.tar.gz.  It will also attempt to upload it to github releases.  This file can also be copied to a similar directory on a mac and uploaded from there via `yarn uploadall`.

If it creates a file with something like v94 instead of v6, this is not what you want and a script got the wrong napi version.  Try also removing the build directory - `rm -rf node_modules yarn.lock build && yarn install`.

Uploading requires a GITHUB_TOKEN env variable to be set to grant permission.

## NDI Cameras

The following NDI capable cameras are good candidates for use with CrewTimer Video Recorder.  The ones marked as *Verified* are known to stream continuously without issue.  The others are untested by CrewTimer.  If you find an NDI camera that works well for you please let me know so I can add it to the list.

| Price | Zoom    | PTZ | FPS | Weather | Verified | Model                                                                                                                         |
| ----- | ------- | --- | --- | ------- | -------- | ----------------------------------------------------------------------------------------------------------------------------- |
| $1450 | 20X     | Yes | 120 |         | **Yes**  | [AIDA PTZ-NDI3-X20](https://usbroadcast.co/product/aida-imaging-ptz-ndi3-x20b-full-hd-ndihx2-ptz-camera-20x-zoom-black/)      |
| $1075 | 30X     |     | 60  |         | **Yes**  | [AIDA UHD-NDI3-X30](https://usbroadcast.co/product/aida-imaging-uhd-ndihx3-ip-srt-hdmi-poe-30x-zoom-pov-camera/)              |
| $799  | 30X     | Yes | 60  |         | **Yes**  | [AVKANS NDI PTZ Camera](https://a.co/d/1FIcJW9)                                                                               |
| $719  | 30X     | Yes | 60  |         | **Yes**  | [SMTAV BX30N](https://www.smtav.com/collections/ndi/products/smtav-ai-tracking-ndi-ptz-camera-30x-optics-zoom)                |
| $775  | 1X C/CS |     | 120 | IP67    |          | [AIDA HD-NDI3-IP67](https://usbroadcast.co/product/aida-imaging-ndihx3-ip67-weatherproof-pov-camera/)                         |
| $669  | 20X     | Yes | 60  |         |          | [SMTAV BX20N](https://www.smtav.com/collections/ndi/products/smtav-20x-optics-zoom-ai-tracking-ndi-ptz-camera-bx20n-w)        |
| $1295 | 20X     |     | 60  |         |          | [Birddog Maki Ultra 4K](https://www.bhphotovideo.com/c/product/1820220-REG/birddog_bdpmku20xw_birddog_maki_ultra_white.html/) |
| $1295 | 30x     | Yes | 60  |         |          | [BirdDog Eyes P200](https://www.bhphotovideo.com/c/product/1434646-REG/birddog_bdp200b_eyes_p200_1080p_full.html)             |

 **Note**:Cameras often needed software upgrades to operate properly.  Be sure to update software as soon as you purchase one.  The SMTAV BA series had unreliable NDI timestamps and are not recommended.
