/**
 * This module provides native C++ functionality as a Node.js addon,
 * specifically designed for Electron applications.
 * @module crewtimer_video_recorder
 */
declare module 'crewtimer_video_recorder' {
  interface MessageBase {
    op: string;
  }

  interface MessageResponseBase {
    status: string;
  }

  interface TestMessage extends MessageBase {
    op: 'test';
  }

  export function nativeVideoRecorder(
    message: MessageBase,
  ): MessageResponseBase;

  export function nativeVideoRecorder(
    message: TestMessage,
  ): MessageResponseBase;
}
