{
  "targets": [
    {
      "target_name": "crewtimer_video_recorder",
      "sources": [ "src/RecorderAPI.cpp",
                  "src/FFRecorder.cpp",
                  "src/FrameProcessor.cpp",
                  "src/MulticastReceiver.cpp",
                  "src/NdiReader.cpp",
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
          "/Users/glenne/ffmpeg-built-win/include",
        ],
        "link_settings": {
            "libraries": [
                "/Users/glenne/ffmpeg-built-win/lib/libavcodec.a",
                "/Users/glenne/ffmpeg-built-win/lib/libavformat.a",
                "/Users/glenne/ffmpeg-built-win/lib/libavutil.a",
                "/Users/glenne/ffmpeg-built-win/lib/libswscale.a",
                "Bcrypt.lib", "Mfuuid.lib", "Strmiids.lib"
            ],
          'library_dirs': ['/Users/glenne/ffmpeg-built-win/lib']
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
