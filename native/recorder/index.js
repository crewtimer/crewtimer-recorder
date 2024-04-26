const addon = require('bindings')('crewtimer_video_recorder');

module.exports = {nativeVideoRecorder: addon.nativeVideoRecorder
};