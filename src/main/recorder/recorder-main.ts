import { nativeVideoRecorder } from 'crewtimer_video_recorder';
import { ipcMain } from 'electron';

export function startRecording() {
  try {
    const ret = nativeVideoRecorder({ op: 'start-recording' });
    return ret;
  } catch (err) {
    return { status: `${err instanceof Error ? err.message : err}` };
  }
}

export function stopRecording() {
  try {
    const ret = nativeVideoRecorder({ op: 'stop-recording' });
    return ret;
  } catch (err) {
    return { status: `${err instanceof Error ? err.message : err}` };
  }
}

export function junkie() {}
