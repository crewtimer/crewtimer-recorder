const addon = require('bindings')('crewtimer_video_recorder');

module.exports = {
  nativeVideoRecorder: addon.nativeVideoRecorder,
  setNativeMessageCallback: addon.setNativeMessageCallback,
  requestLocalNetworkPermission: addon.requestLocalNetworkPermission,
  shutdownRecorder: addon.shutdownRecorder,
  setLogFile: addon.setLogFile,
};
