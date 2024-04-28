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
