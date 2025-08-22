{
  "targets": [
    {
      "target_name": "crewtimer_video_recorder",
      "sources": [ "src/RecorderAPI.cpp",
                  "src/FFRecorder.cpp",
                  "src/FrameProcessor.cpp",
                  "src/MulticastReceiver.cpp",
                  "src/NdiReader.cpp",
                  "src/NullRecorder.cpp",
                  "src/VideoUtils.cpp",
                  "src/util.cpp",
                  "src/event/NativeEvent.cpp",
                  "src/visca/ViscaTcpClient.cpp",
                  "src/mdns/ndi_mdns.cpp"],
      "defines": [ "USE_FFMPEG" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "./src"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "conditions": [
        ['OS=="mac"', {
          "include_dirs": [
              "./lib-build/ffmpeg-static-mac/include",
              "/Library/NDI\ SDK\ for\ Apple/include/",
            ],
          "link_settings": {
            "libraries": [
              "../lib-build/ffmpeg-static-mac/lib/libavcodec.a",
              "../lib-build/ffmpeg-static-mac/lib/libavformat.a",
              "../lib-build/ffmpeg-static-mac/lib/libavutil.a",
              "../lib-build/ffmpeg-static-mac/lib/libswscale.a",
              "../lib/libndi.dylib",
              "-framework VideoToolbox",
              "-framework AudioToolbox",
              "-framework CoreMedia",
              "-framework CoreVideo",
              "-framework CoreServices",
              "-framework CoreFoundation"],
            'library_dirs': ['../lib-build/ffmpeg-static-mac/lib','../lib']
          },
          'xcode_settings': {
            'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
            'CLANG_CXX_LIBRARY': 'libc++'
          }
      }],

      ['OS=="win"', {
        "include_dirs": [
          "./lib-build/ffmpeg-static-win/include",
          "C:/Program Files/NDI/NDI 5 SDK/Include",
          "C:/Program Files (x86)/NDI/NDI 5 SDK/Include",
           "C:/Program Files/NDI/NDI 6 SDK/Include",
          "C:/Program Files (x86)/NDI/NDI 6 SDK/Include"
        ],
        "link_settings": {
            "libraries": [
              "../lib-build/ffmpeg-static-win/lib/libavcodec.a",
              "../lib-build/ffmpeg-static-win/lib/libavformat.a",
              "../lib-build/ffmpeg-static-win/lib/libavutil.a",
              "../lib-build/ffmpeg-static-win/lib/libswscale.a",
              "../lib/Processing.NDI.Lib.x64.lib",
              "Bcrypt.lib", "Mfuuid.lib", "Strmiids.lib"
            ],
          'library_dirs': ["../lib-build/ffmpeg-static-win/staticlib","../lib"]
          }
        }],
      ],
      'msvs_settings': {
            'VCCLCompilerTool': {
              'ExceptionHandling': 1
            }
          },
      "defines": [ "NAPI_CPP_EXCEPTIONS",
                  "NAPI_VERSION=<(napi_build_version)", ],
      "cflags!": [ "-fexceptions"],
      "cflags_cc!": [ "-fexceptions" ]
    }
  ]
}
