import { HandlerResponse } from '../../src/renderer/msgbus/MsgbusTypes';
import { RecorderMessage } from '../../src/renderer/recorder/RecorderTypes';

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

  type CallbackFunc = ({
    sender,
    content,
  }: {
    sender: string;
    content: { [key: string]: any };
  }) => void;
  export function setNativeMessageCallback(func: CallbackFunc): void;
}
