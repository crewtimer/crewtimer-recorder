const addon = require('bindings')('crewtimer_video_recorder');

module.exports = {
  nativeVideoRecorder: addon.nativeVideoRecorder,
  setNativeMessageCallback: addon.setNativeMessageCallback,
  shutdownRecorder: addon.shutdownRecorder,
  setLogFile: addon.setLogFile,
};
