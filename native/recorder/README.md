# CrewTimer Video Recorder

An electron native module for recording video for use with CrewTimer.  The package uses 'prebuild' to build native versions and store them in github releases so projects which use this module do not have to build the module.

## Toolchains

Building this native module on windows requires building both opencv and ffmpeg from source to allow static linking to the C++ code.  This requires build tools and a few custom scripts.

### MacOS Toolchain

On MacOS, the following brew modules are required to be installed.

Install brew from [brew.sh](https://brew.sh)

```bash
brew install nvm
brew install nasm
brew install yasm
brew install pkg-config
brew install cmake
```

### Windows Toolchain

A unix like environment is needed to build ffmpeg and opencv.  Cygwin is used to establish a unix-like environment.  Install the following:

- [nvm for windows](https://github.com/coreybutler/nvm-windows/releases)
- [git for windows](https://gitforwindows.org/)
- [Visual Studio Community]() with C++ addon
- [cmake](https://cmake.org/download/)
- Python.  Just type `python` on windows to get prompted to install. It is installed already on macos.
- [Cygwin 64 bit](https://www.cygwin.com/install.html).  Add 'yasm' and 'make' modules.

Either check out the git repo or if in a Macos Parallels Desktop, map a network drive to share the git repo.

In windows explorer, navigate to the scripts/ folder and double click on the Cygwin-vstudio.bat file.  This will open a bash terminal with the visual studio tools available from the command line.

## Building the prebuilt binary

Set up nvm/node:

```bash
nvm install 18
nvm use 18
npm i -g yarn
```

Build ffmpeg and opencv:

```bash
cd crewtimer-video-review/native/ffreader
./scripts/build-opencv.sh
./scripts/build-ffmpeg.sh
```

Build the module and upload to github:

```bash
yarn install
yarn build:mac # if mac
yarn build:win # if windows
```

The result is placed into a file such as prebuilds/crewtimer_video_reader-v1.0.2-napi-v6-win32-x64.tar.gz.

The `yarn prebuild` command will also upload the binary module to github if a ~/.prebuildrc file with a github token is present such as 

```txt
upload=ghp_kQ04DpisXo2hTiLt2syssyssysysysysysy
token=ghp_kQ04DpisXo2hTiLt2syssyssysysysysysy
```

Optionally manually upload the tar.gz file to [github releases](https://github.com/crewtimer/crewtimer-video-review/releases).


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
