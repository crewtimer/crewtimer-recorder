#include "VideoReader.hpp"
#include <chrono>
#include <pylon/BaslerUniversalInstantCamera.h>
#include <pylon/PylonIncludes.h>

using namespace Pylon;
using namespace Basler_UniversalCameraParams;
using namespace Basler_UniversalStreamParams;

using namespace std;
using namespace std::chrono;
class BaslerReader : public VideoReader, public CImageEventHandler {
  std::shared_ptr<FrameProcessor> frameProcessor;
  std::shared_ptr<CBaslerUniversalInstantCamera> camera;
  std::thread readerThread;
  std::atomic<bool> keepRunning;
  static CImageFormatConverter formatConverter;

  class BaslerFrame : public Frame {
    const CGrabResultPtr &resultPtr;
    CPylonImage pylonImage;

  public:
    BaslerFrame(const CGrabResultPtr &resultPtr) : resultPtr(resultPtr) {

      formatConverter.Convert(pylonImage, resultPtr);
      data = (uint8_t *)pylonImage.GetBuffer();
      xres = pylonImage.GetWidth();
      yres = pylonImage.GetHeight();
      timestamp = resultPtr->GetTimeStamp();
      frame_rate_N = 60;
      frame_rate_D = 1;
      pixelFormat = Frame::PixelFormat::BGR;
    }
    virtual ~BaslerFrame() override {}
  };

public:
  BaslerReader() { formatConverter.OutputPixelFormat = PixelType_BGR8packed; }
  virtual std::string
  open(std::shared_ptr<FrameProcessor> frameProcessor) override {
    std::string ret = "";
    PylonInitialize();

    try {
      this->frameProcessor = frameProcessor;
      camera = std::make_shared<CBaslerUniversalInstantCamera>(
          CTlFactory::GetInstance().CreateFirstDevice());
      // camera->PixelFormat.SetValue(PixelFormat_Mono8);
      // Print the model name of the camera.
      cout << "Using device " << camera->GetDeviceInfo().GetModelName() << endl;

      auto &nodemap = camera->GetNodeMap();

      // Open the camera for accessing the parameters.
      camera->Open();

      // Get camera device information.
      cout << "Camera Device Information" << endl
           << "=========================" << endl;
      cout << "Vendor           : "
           << CStringParameter(nodemap, "DeviceVendorName").GetValue() << endl;
      cout << "Model            : "
           << CStringParameter(nodemap, "DeviceModelName").GetValue() << endl;
      cout << "Firmware version : "
           << CStringParameter(nodemap, "DeviceFirmwareVersion").GetValue()
           << endl
           << endl;

      // // Camera settings.
      // cout << "Camera Device Settings" << endl
      //      << "======================" << endl;

      // // Set the AOI:

      // // Get the integer nodes describing the AOI.
      // CIntegerParameter offsetX(nodemap, "OffsetX");
      // CIntegerParameter offsetY(nodemap, "OffsetY");
      // CIntegerParameter width(nodemap, "Width");
      // CIntegerParameter height(nodemap, "Height");

      // // On some cameras, the offsets are read-only.
      // // Therefore, we must use "Try" functions that only perform the action
      // // when parameters are writable. Otherwise, we would get an exception.
      // offsetX.TrySetToMinimum();
      // offsetY.TrySetToMinimum();

      // // Some properties have restrictions.
      // // We use API functions that automatically perform value corrections.
      // // Alternatively, you can use GetInc() / GetMin() / GetMax() to make
      // sure
      // // you set a valid value.
      // width.SetValue(640, IntegerValueCorrection_Nearest);
      // height.SetValue(480, IntegerValueCorrection_Nearest);

      // cout << "OffsetX          : " << offsetX.GetValue() << endl;
      // cout << "OffsetY          : " << offsetY.GetValue() << endl;
      // cout << "Width            : " << width.GetValue() << endl;
      // cout << "Height           : " << height.GetValue() << endl;

      // // Access the PixelFormat enumeration type node.
      // CEnumParameter pixelFormat(nodemap, "PixelFormat");

      // // Remember the current pixel format.
      // String_t oldPixelFormat = pixelFormat.GetValue();
      // cout << "Old PixelFormat  : " << oldPixelFormat << endl;

    } catch (const GenericException &e) {
      // Error handling.
      auto msg = std::string("An exception occurred.") + e.GetDescription();
      cerr << msg << endl;
      ret = msg;
    }
    return ret;
  };

  std::chrono::steady_clock::time_point startTime;
  int frameCounter = 0;
  void OnImageGrabbed(CInstantCamera & /*camera*/,
                      const CGrabResultPtr &ptrGrabResult) override {
    if (!ptrGrabResult->GrabSucceeded()) {
      cout << "Error: " << std::hex << ptrGrabResult->GetErrorCode() << std::dec
           << " " << ptrGrabResult->GetErrorDescription() << endl;
      camera->StopGrabbing();
      keepRunning = false;
      return;
    }
    frameCounter++;
    if (frameCounter % 200 == 0) {
      auto now = high_resolution_clock::now();
      auto elapsed = duration_cast<milliseconds>(now - startTime);
      cout << "fps=" << 1000 * 200.0 / elapsed.count() << endl;
      auto pixelType = ptrGrabResult->GetPixelType();
      // Access the image data.
      cout << "SizeX: " << ptrGrabResult->GetWidth() << endl;
      cout << "SizeY: " << ptrGrabResult->GetHeight() << endl;
      cout << "PixelType: " << pixelType << endl;
      const uint8_t *pImageBuffer = (uint8_t *)ptrGrabResult->GetBuffer();
      cout << "Gray value of first pixel: " << (uint32_t)pImageBuffer[0] << endl
           << endl;

      startTime = now;
    }

    auto txframe = std::make_shared<BaslerFrame>(ptrGrabResult);

    frameProcessor->addFrame(txframe);

    // cout << "CSampleImageEventHandler::OnImageGrabbed called." << std::endl;
    // cout << std::endl;
    // cout << std::endl;
  }

  int run() {
    camera->MaxNumBuffer = 100;
    // camera->Width = 1280;
    // camera->Height = 720;
    // camera->PixelFormat = PixelFormat_YCbCr422_8;
    startTime = std::chrono::steady_clock::now();
    frameCounter = 0;

    camera->RegisterImageEventHandler(this, RegistrationMode_Append,
                                      Cleanup_Delete);
    // Start the grabbing using the grab loop thread, by setting the
    // grabLoopType parameter to GrabLoop_ProvidedByInstantCamera. The grab
    // results are delivered to the image event handlers. The
    // GrabStrategy_OneByOne default grab strategy is used.
    camera->StartGrabbing(GrabStrategy_OneByOne,
                          GrabLoop_ProvidedByInstantCamera);
    while (keepRunning) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
  }

  int run1() {

    static const uint32_t c_countOfImagesToGrab = 100;
    // The parameter MaxNumBuffer can be used to control the count of buffers
    // allocated for grabbing. The default value of this parameter is 10.
    camera->MaxNumBuffer = 100;
    camera->Width = 640;
    camera->Height = 480;

    // Start the grabbing of c_countOfImagesToGrab images.
    // The camera device is parameterized with a default configuration which
    // sets up free-running continuous acquisition.
    camera->StartGrabbing(c_countOfImagesToGrab);

    // This smart pointer will receive the grab result data.
    CGrabResultPtr ptrGrabResult;

    // camera->StopGrabbing() is called automatically by the RetrieveResult()
    // method when c_countOfImagesToGrab images have been retrieved.
    while (keepRunning && camera->IsGrabbing()) {
      // Wait for an image and then retrieve it. A timeout of 5000 ms is used.
      camera->RetrieveResult(5000, ptrGrabResult,
                             TimeoutHandling_ThrowException);

      // Image grabbed successfully?
      if (ptrGrabResult->GrabSucceeded()) {
        // Access the image data.
        cout << "SizeX: " << ptrGrabResult->GetWidth() << endl;
        cout << "SizeY: " << ptrGrabResult->GetHeight() << endl;
        const uint8_t *pImageBuffer = (uint8_t *)ptrGrabResult->GetBuffer();
        cout << "Gray value of first pixel: " << (uint32_t)pImageBuffer[0]
             << endl
             << endl;

      } else {
        cout << "Error: " << std::hex << ptrGrabResult->GetErrorCode()
             << std::dec << " " << ptrGrabResult->GetErrorDescription() << endl;
      }
    }

    camera = nullptr;

    // Releases all pylon resources.
    PylonTerminate();
    return 0;
  }
  virtual std::string start() override {
    keepRunning = true;
    readerThread = std::thread([this]() { run(); });
    readerThread.join();
    return "";
  };
  virtual std::string stop() override {
    keepRunning = false;
    return "";
  };
  virtual ~BaslerReader() {}
};

CImageFormatConverter BaslerReader::formatConverter;

std::shared_ptr<VideoReader> createBaslerReader() {
  return std::shared_ptr<VideoReader>(new BaslerReader());
}