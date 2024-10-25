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
                  "src/util.cpp"],
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
              "/Users/glenne/ffmpeg-built-mac/include",
              "/Library/NDI\ SDK\ for\ Apple/include/",
            ],
          "link_settings": {
            "libraries": [
                "/Users/glenne/ffmpeg-built-mac/lib/libavcodec.a",
                "/Users/glenne/ffmpeg-built-mac/lib/libavdevice.a",
                "/Users/glenne/ffmpeg-built-mac/lib/libavfilter.a",
                "/Users/glenne/ffmpeg-built-mac/lib/libavformat.a",
                "/Users/glenne/ffmpeg-built-mac/lib/libavutil.a",
                "/Users/glenne/ffmpeg-built-mac/lib/libswresample.a",
                "/Users/glenne/ffmpeg-built-mac/lib/libswscale.a",
                "/usr/local/lib/libndi.dylib",
                "-framework VideoToolbox",
                "-framework AudioToolbox",
                "-framework CoreMedia",
                "-framework CoreVideo",
                "-framework CoreServices",
                "-framework CoreFoundation"],

            'library_dirs': ['/Users/glenne/ffmpeg-built-mac/lib']
          },
          'xcode_settings': {
            'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
            'CLANG_CXX_LIBRARY': 'libc++'
          }
      }],

      ['OS=="win"', {
        "include_dirs": [
          "y:/ffmpeg-built-win/include",
          "C:/Program Files/NDI/NDI 5 SDK/Include"
        ],
        "link_settings": {
            "libraries": [
                "y:/ffmpeg-built-win/lib/libavcodec.a",
                "y:/ffmpeg-built-win/lib/libavformat.a",
                "y:/ffmpeg-built-win/lib/libavutil.a",
                "y:/ffmpeg-built-win/lib/libswscale.a",
                "y:/ffmpeg-built-win/lib/libswresample.a",
                "Processing.NDI.Lib.x64.lib",
                "Bcrypt.lib", "Mfuuid.lib", "Strmiids.lib"
            ],
          'library_dirs': ['y:/ffmpeg-built-win/lib', "C:/Program Files/NDI/NDI 5 SDK/lib/x64"]
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
