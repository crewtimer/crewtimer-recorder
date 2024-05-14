import { HandlerResponse } from '../msgbus/MsgbusTypes';
import {
  StartRecorderMessage,
  RecorderMessage,
  RecordingLog,
  GrabFrameResponse,
  RecordingStatus,
} from './RecorderTypes';
import { getRecordingProps, setFrameGrab } from './RecorderData';

export const startRecording = () => {
  return window.msgbus.sendMessage<StartRecorderMessage, HandlerResponse>(
    'recorder',
    {
      op: 'start-recording',
      props: getRecordingProps(),
    },
  );
};
export const stopRecording = () => {
  return window.msgbus.sendMessage<RecorderMessage, HandlerResponse>(
    'recorder',
    {
      op: 'stop-recording',
    },
  );
};

export const queryRecordingStatus = () => {
  return window.msgbus.sendMessage<RecorderMessage, RecordingStatus>(
    'recorder',
    {
      op: 'recording-status',
    },
  );
};

export const queryRecordingLog = () => {
  return window.msgbus.sendMessage<RecorderMessage, RecordingLog>('recorder', {
    op: 'recording-log',
  });
};

export const requestVideoFrame = async () => {
  window.msgbus
    .sendMessage<RecorderMessage, GrabFrameResponse>('recorder', {
      op: 'grab-frame',
    })
    .then((frame) => {
      if (frame.status === 'OK') {
        setFrameGrab(frame);
      } else {
        setFrameGrab(undefined);
      }
      return frame;
    })
    .catch((err) => {
      console.error(err);
      return undefined;
    });
};
