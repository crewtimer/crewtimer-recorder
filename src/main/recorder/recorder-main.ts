import { app } from 'electron';
import {
  shutdownRecorder,
  nativeVideoRecorder,
  setNativeMessageCallback,
} from 'crewtimer_video_recorder';
import { msgbus } from '../msgbus/msgbus-util';
import { setStoredValue } from '../store/store';
import {
  RecorderMessage,
  RecorderResponse,
} from '../../renderer/recorder/RecorderTypes';

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
      const ret = nativeVideoRecorder(message);
      return ret;
    },
  );
}

export function stopRecording() {
  try {
    const ret = nativeVideoRecorder({ op: 'stop-recording' });
    shutdownRecorder();
    return ret;
  } catch (err) {
    return { status: `${err instanceof Error ? err.message : err}` };
  }
}
