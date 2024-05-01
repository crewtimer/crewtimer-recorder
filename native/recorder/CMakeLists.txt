cmake_minimum_required(VERSION 3.5) # Set minimum CMake version
project(ctrecorder VERSION 1.0 LANGUAGES CXX)

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.  Use cmake -DCMAKE_BUILD_TYPE=Debug for Debug.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui, ccmake
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Declare an option for enabling Basler support
option(ENABLE_BASLER "Enable support for Basler cameras" OFF)
option(USE_OPENCV "Use OpenCV features" ON)
option(USE_FFMPEG "Use OpenCV features" ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

message(STATUS "${CMAKE_PREFIX_PATH}")
message(STATUS "updated:${CMAKE_PREFIX_PATH}")

set(SOURCE_FILES
    src/ctrecorder.cpp
    src/FrameProcessor.cpp
    src/FFRecorder.cpp
    src/util.cpp
    src/NdiReader.cpp
)

# prefer static libs
if(APPLE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dylib")
    set(EXTRA_LIBS z bz2 lzma pthread)
    set(FFMPEG_FOLDER "/Users/glenne/ffmpeg-built-mac")
    # set(USE_OPENCV OFF CACHE BOOL "Use OPENCV features" FORCE)
    list(APPEND SOURCE_FILES
        src/MulticastReceiver.cpp
    )
    find_library(NDI_LIBRARIES ndi REQUIRED)
elseif(WIN32)
    message(STATUS "Win32 section")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dll" ".lib")
    set(LIB_PREFIX lib)
    set(EXTRA_LIBS  Ws2_32 Secur32 Bcrypt mfplat
    mfuuid strmiids)
    set(FFMPEG_FOLDER "C:/Users/glenne/git/crewtimer-connect/native/ffreader/src/ffmpeg-built-win")
    add_compile_definitions(USE_FFMPEG)
    set(USE_OPENCV OFF CACHE BOOL "Use OPENCV features" FORCE)
    # add_compile_definitions(USE_OPENCV)
    set(NDI_LIBRARIES "C:/Program Files/NDI/NDI 5 SDK/Lib/x64/Processing.NDI.Lib.x64.lib")

else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
    add_compile_definitions(USE_FFMPEG)
    add_compile_definitions(USE_OPENCV)
    list(APPEND SOURCE_FILES
        src/MulticastReceiver.cpp
    )
endif()

# set(FFMPEG_FOLDER "/usr/local/Cellar/ffmpeg/7.0")

list(INSERT CMAKE_PREFIX_PATH 0 "${FFMPEG_FOLDER}")
# list(APPEND CMAKE_PREFIX_PATH "/home/glenne/ndi/NDI/lib/arm-rpi3-linux-gnuabihf")
include_directories(
    src
    ${FFMPEG_FOLDER}/include
    # /usr/local/opt/ffmpeg@4/include
    /home/glenne/ndi/NDI/include
    /usr/include/ndi
    /usr/local/opt/ndi
    /Library/NDI\ SDK\ for\ Apple/include/
    "C:/Program\ Files/NDI/NDI\ 5\ SDK/Include"
)

# Fetch nlohmann/json from GitHub
include(FetchContent)
# FetchContent_Declare(
#   json
#   GIT_REPOSITORY https://github.com/nlohmann/json.git
#   GIT_TAG v3.10.5 # Specify the version you want to use
# )
# FetchContent_MakeAvailable(json)

if (USE_OPENCV)
    add_compile_definitions(USE_OPENCV)
    list(APPEND SOURCE_FILES
        src/OpenCVRecorder.cpp
    )
    # Find OpenCV package in the system
    find_package(OpenCV REQUIRED)

    # Include directories for OpenCV
    include_directories(${OpenCV_INCLUDE_DIRS})
endif()

if (USE_FFMPEG)
    add_compile_definitions(USE_FFMPEG)
    list(APPEND SOURCE_FILES
        src/FFRecorder.cpp
    )
    if (APPLE)
        set(FFMPEG_FRAMEWORKS
            "-framework VideoToolbox"
            "-framework AudioToolbox"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework CoreServices"
            "-framework CoreFoundation")
    endif()
endif()

if(ENABLE_BASLER)
    # Add definitions that might be needed by your project
    add_definitions(-DHAVE_BASLER)
    find_package(pylon 7.3.1 REQUIRED)
    set(BASLER_LIBS pylon::pylon)
    list(APPEND SOURCE_FILES
        src/BaslerReader.cpp
    )
endif()

# Define the executable
add_executable(ctrecorder ${SOURCE_FILES})

# Link nlohmann_json::nlohmann_json to the target
# target_link_libraries(ctrecorder PRIVATE nlohmann_json::nlohmann_json)

find_library(AVFORMAT_LIBRARY ${LIB_PREFIX}avformat REQUIRED)
find_library(AVCODEC_LIBRARY ${LIB_PREFIX}avcodec REQUIRED)
find_library(AVUTIL_LIBRARY ${LIB_PREFIX}avutil REQUIRED)
find_library(SWSCALE_LIBRARY ${LIB_PREFIX}swscale REQUIRED)
find_library(SWSRESAMPLE_LIBRARY ${LIB_PREFIX}swresample REQUIRED)

message(STATUS "avlibs found at ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY} ${SWSRESAMPLE_LIBRARY}")
message(STATUS "opencv found at ${OpenCV_INCLUDE_DIRS} ${OpenCV_LIBS}")
message(STATUS "NDI found at ${NDI_LIBRARIES}")

target_link_libraries(ctrecorder PRIVATE
    ${OpenCV_LIBS} ${NDI_LIBRARIES}
    ${EXTRA_LIBS}
    ${FFMPEG_FRAMEWORKS}
    ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY} ${SWSRESAMPLE_LIBRARY}
    ${BASLER_LIBS}
)
