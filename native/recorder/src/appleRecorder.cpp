#include <CoreMedia/CMSampleBuffer.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <iostream>
#include <stdio.h>

#include "VideoRecorder.hpp"

// See
// https://github.com/obsproject/obs-studio/blob/master/plugins/mac-videotoolbox/encoder.c
// ffmpeg https://ffmpeg.org/doxygen/trunk/videotoolboxenc_8c_source.html
// https://mobisoftinfotech.com/resources/mguide/h264-encode-decode-using-videotoolbox/
//

#define NAL true
CVPixelBufferRef createRedPixelBuffer(int width, int height) {
  CVPixelBufferRef pixelBuffer;
  CVReturn result = CVPixelBufferCreate(
      NULL, width, height, kCVPixelFormatType_32BGRA, NULL, &pixelBuffer);

  if (result != kCVReturnSuccess) {
    printf("Failed to create pixel buffer.\n");
    return NULL;
  }

  CVPixelBufferLockBaseAddress(pixelBuffer, 0);
  uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(pixelBuffer);
  size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t *pixel = baseAddress + y * bytesPerRow + x * 4;
      pixel[0] = 0;   // Blue
      pixel[1] = 0;   // Green
      pixel[2] = 255; // Red
      pixel[3] = 255; // Alpha
    }
  }

  CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
  return pixelBuffer;
}

class AppleRecorder : public VideoRecorder {
  bool active = false;
  int width;
  int height;
  int frameRate;
  FILE *outputFileHandle;
  std::string outputFile;
  std::string tmpFile;
  VTCompressionSessionRef compressionSession;
  int frameIndex;

  void videoCompressionOutputCallback(void *outputCallbackRefCon,
                                      void *sourceFrameRefCon, OSStatus status,
                                      VTEncodeInfoFlags infoFlags,
                                      CMSampleBufferRef sampleBuffer) {

    // Find out if the sample buffer contains an I-Frame.
    // If so we will write the SPS and PPS NAL units to the elementary stream.
    bool isIFrame = false;
    CFArrayRef attachmentsArray =
        CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, 0);
    if (CFArrayGetCount(attachmentsArray)) {
      CFBooleanRef notSync;
      CFDictionaryRef dict =
          (CFDictionaryRef)CFArrayGetValueAtIndex(attachmentsArray, 0);
      bool keyExists = CFDictionaryGetValueIfPresent(
          dict, kCMSampleAttachmentKey_NotSync, (const void **)&notSync);
      // An I-Frame is a sync frame
      isIFrame = !keyExists || !CFBooleanGetValue(notSync);
    }

    std::cout << "Frame #" << frameIndex << " isIFrame: " << isIFrame
              << std::endl;

    static const uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
    if (isIFrame) {
      CMFormatDescriptionRef description =
          CMSampleBufferGetFormatDescription(sampleBuffer);

      // Find out how many parameter sets there are
      size_t numberOfParameterSets;
      CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
          description, 0, NULL, NULL, &numberOfParameterSets, NULL);

      // Write each parameter set to the elementary stream
      for (int i = 0; i < numberOfParameterSets; i++) {
        const uint8_t *parameterSetPointer;
        size_t parameterSetLength;
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            description, i, &parameterSetPointer, &parameterSetLength, NULL,
            NULL);

        // Write the parameter set to the elementary stream
        fwrite(&startCode, 1, sizeof(startCode), outputFileHandle);
        fwrite(&parameterSetPointer, 1, parameterSetLength, outputFileHandle);
      }
    }

    // Get a pointer to the raw AVCC NAL unit data in the sample buffer
    size_t blockBufferLength;
    uint8_t *bufferDataPointer = NULL;
    CMBlockBufferRef dataBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    CMBlockBufferGetDataPointer(dataBuffer, 0, NULL, &blockBufferLength,
                                (char **)&bufferDataPointer);

    std::cout << "Buffer len=" << blockBufferLength << std::endl;

#if NAL
    // Loop through all the NAL units in the block buffer
    // and write them to the elementary stream with
    // start codes instead of AVCC length headers
    size_t bufferOffset = 0;
    static const int AVCCHeaderLength = 4;
    while (bufferOffset < blockBufferLength - AVCCHeaderLength) {
      // Read the NAL unit length
      uint32_t NALUnitLength = 0;
      memcpy(&NALUnitLength, bufferDataPointer + bufferOffset,
             AVCCHeaderLength);
      // Convert the length value from Big-endian to Little-endian
      NALUnitLength = CFSwapInt32BigToHost(NALUnitLength);
      // Write start code to the elementary stream
      fwrite(&startCode, 1, sizeof(startCode), outputFileHandle);
      // Write the NAL unit without the AVCC length header to the elementary
      // stream
      std::cout << "Writing NAL l=" << NALUnitLength << std::endl;
      fwrite(bufferDataPointer + bufferOffset + AVCCHeaderLength, 1,
             NALUnitLength, outputFileHandle);

      // Move to the next NAL unit in the block buffer
      bufferOffset += AVCCHeaderLength + NALUnitLength;
    }
    // Before decompress we will write elementry data to .h264 file in document
    // directory you can get that file using iTunes => Apps = > FileSharing =>
    // AVEncoderDemo
#else
    if (status == noErr && sampleBuffer != NULL) {
      fwrite(bufferDataPointer, 1, blockBufferLength, outputFile);
    }
#endif
  }

  static void videoCompressionOutputCallbackWrapper(
      void *outputCallbackRefCon, void *sourceFrameRefCon, OSStatus status,
      VTEncodeInfoFlags infoFlags, CMSampleBufferRef sampleBuffer) {
    AppleRecorder *ptr = (AppleRecorder *)outputCallbackRefCon;
    if (ptr) {
      ptr->videoCompressionOutputCallback(outputCallbackRefCon,
                                          sourceFrameRefCon, status, infoFlags,
                                          sampleBuffer);
    }
  }

public:
  std::string openVideoStream(std::string directory, std::string filename,
                              int width, int height, float fps) {
    this->width = width;
    this->height = height;
    this->frameIndex = 0;

    // Set up output format description
    // CFMutableDictionaryRef outputFormatDescription;
    // CMVideoFormatDescriptionCreate(NULL, kCMVideoCodecType_H264, width,
    // height, NULL, &outputFormatDescription);

    // Create compression session
    VTCompressionSessionCreate(
        NULL, // use default allocator
        width, height, kCMVideoCodecType_H264,
        NULL, // use default encoder
        NULL, // no source attributes specified
        NULL, // use default compressed data allocator
        videoCompressionOutputCallbackWrapper, // callback
        this,                                  // callback arg
        &compressionSession);

    const int32_t key_frame_interval = fps * 2;
    const float expected_framerate = 1.0f / fps;
    CFNumberRef MaxKeyFrameInterval = CFNumberCreate(
        kCFAllocatorDefault, kCFNumberSInt32Type, &key_frame_interval);
    CFNumberRef ExpectedFrameRate = CFNumberCreate(
        kCFAllocatorDefault, kCFNumberFloat32Type, &expected_framerate);
    // Set properties for the compression session
    VTSessionSetProperty(compressionSession, kVTCompressionPropertyKey_RealTime,
                         kCFBooleanFalse);
    VTSessionSetProperty(compressionSession,
                         kVTCompressionPropertyKey_ProfileLevel,
                         kVTProfileLevel_H264_Baseline_AutoLevel);
    VTSessionSetProperty(compressionSession,
                         kVTCompressionPropertyKey_MaxKeyFrameInterval,
                         MaxKeyFrameInterval);
    VTSessionSetProperty(compressionSession,
                         kVTCompressionPropertyKey_ExpectedFrameRate,
                         MaxKeyFrameInterval);
    CFRelease(MaxKeyFrameInterval);
    CFRelease(ExpectedFrameRate);

    outputFile = filename + ".mp4";
    tmpFile = directory + "/" + "tmp-" + outputFile;
    outputFile = directory + "/" + outputFile;
    // Open the output file
    this->outputFileHandle = fopen(tmpFile.c_str(), "wb");
    if (this->outputFileHandle == NULL) {
      auto msg = "Failed to open output file.";
      std::cerr << msg << std::endl;
      return msg;
    }

    // Start the compression session
    VTCompressionSessionPrepareToEncodeFrames(compressionSession);

    CFBooleanRef b = NULL;
    auto code = VTSessionCopyProperty(
        compressionSession,
        kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, NULL,
        &b);

    if (code == noErr)
      printf("session created with hardware encoding\n");

    if (b != NULL)
      CFRelease(b);

    return "";
  }
  std::string writeVideoFrame(NDIlib_video_frame_v2_t &video_frame) {
    std::cout << "Write Frame #" << frameIndex
              << ", l=" << video_frame.line_stride_in_bytes * video_frame.yres
              << std::endl;
    // Prepare the frame for encoding (assume `pixelBuffer` contains the frame
    // data)
    // kCMPixelFormat_422YpCbCr8
    // CVPixelBufferRef pixelBuffer = createRedPixelBuffer(width, height);
    // Create or obtain the pixel buffer for the frame

    // Create a CVPixelBufferRef from the RGBA frame data
    CVPixelBufferRef pixelBuffer;
    CVPixelBufferCreateWithBytes(NULL, width, height, kCMPixelFormat_422YpCbCr8,
                                 video_frame.p_data, width * 2, NULL, NULL,
                                 NULL, &pixelBuffer);

    // Encode the frame
    VTCompressionSessionEncodeFrame(compressionSession, pixelBuffer,
                                    CMTimeMake(frameIndex++, frameRate),
                                    CMTimeMake(1, frameRate), NULL, NULL, NULL);

    // Release the pixel buffer
    CVPixelBufferRelease(pixelBuffer);
    return "";
  }

  std::string closeVideoStream() {
    if (active) {

      // Complete the compression session
      VTCompressionSessionCompleteFrames(compressionSession, kCMTimeInvalid);
      // CMTimeMake(frameIndex++, frameRate));

      // Clean up the compression session
      VTCompressionSessionInvalidate(compressionSession);
      CFRelease(compressionSession);

      // Close the output file
      fclose(outputFileHandle);

      active = false;
      // Attempt to rename the file
      if (std::rename(tmpFile.c_str(), outputFile.c_str()) == 0) {
        // std::cout << "File successfully renamed from " << tmpFile << " to "
        //           << outputFile << std::endl;
      } else {
        // If renaming failed, print an error message
        auto msg = "Error renaming file";
        std::stderr << msg << std::endl;
        return msg;
      }
    }
    return "";
  }
  ~AppleRecorder() { closeVideoStream(); }
};

std::shared_ptr<VideoRecorder> createAppleRecorder() {
  return std::shared_ptr<AppleRecorder>(new AppleRecorder());
}