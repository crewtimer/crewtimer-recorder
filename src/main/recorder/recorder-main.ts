import { app } from 'electron';
import {
  nativeVideoRecorder,
  setNativeMessageCallback,
} from 'crewtimer_video_recorder';
import { msgbus } from '../msgbus/msgbus-util';
import {
  RecorderMessage,
  RecorderResponse,
} from '../../renderer/recorder/RecorderApi';
import { setStoredValue } from '../store/store';

app.on('ready', () => {
  setNativeMessageCallback(({ sender, content }) => {
    if (sender === 'guide-config') {
      setStoredValue('guide', content);
    }
  });
});

export function initRecorder() {
  msgbus.addSubscriber<RecorderMessage, RecorderResponse>(
    'recorder',
    async (_dest: string, message: RecorderMessage) => {
      // console.log(`Received message to recorder: ${JSON.stringify(message)}`);
      const ret = nativeVideoRecorder(message);
      return ret;
    },
  );
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
