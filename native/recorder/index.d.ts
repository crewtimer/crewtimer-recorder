import { HandlerResponse } from '../../src/renderer/msgbus/MsgbusTypes';
import { RecorderMessage } from '../../src/renderer/recorder/RecorderApi';

/**
 * This module provides native C++ functionality as a Node.js addon,
 * specifically designed for Electron applications.
 * @module crewtimer_video_recorder
 */
declare module 'crewtimer_video_recorder' {
  export function nativeVideoRecorder(
    message: RecorderMessage,
  ): HandlerResponse;

  export function nativeVideoRecorder(
    message: RecorderMessage,
  ): HandlerResponse;
}
