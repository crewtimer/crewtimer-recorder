import { HandlerResponse } from '../msgbus/MsgbusTypes';
import {
  StartRecorderMessage,
  RecorderMessage,
  RecordingLog,
  GrabFrameResponse,
  RecordingStatus,
  CameraListResponse,
  DefaultRecordingProps,
} from './RecorderTypes';
import {
  getFrameGrab,
  getGuide,
  getRecordingProps,
  setFrameGrab,
  setIsRecording,
  setRecordingStartTime,
} from './RecorderData';
import { showErrorDialog } from '../components/ErrorDialog';

export const startRecording = () => {
  setRecordingStartTime(Date.now());
  setIsRecording(true);
  // console.log(`recording props: ${JSON.stringify(getRecordingProps())}`);
  const recordingProps = { ...DefaultRecordingProps, ...getRecordingProps() };
  recordingProps.recordingDuration = Number(recordingProps.recordingDuration);
  const cropArea = { ...recordingProps.cropArea };
  const guide = getGuide();
  return window.msgbus.sendMessage<StartRecorderMessage, HandlerResponse>(
    'recorder',
    {
      op: 'start-recording',
      props: { ...recordingProps, cropArea, guide },
    },
  );
};
export const stopRecording = () => {
  setIsRecording(false);
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

export const queryCameraList = () => {
  return window.msgbus.sendMessage<RecorderMessage, CameraListResponse>(
    'recorder',
    {
      op: 'get-camera-list',
    },
  );
};

export const requestVideoFrame = async () => {
  window.msgbus
    .sendMessage<RecorderMessage, GrabFrameResponse>('recorder', {
      op: 'grab-frame',
    })
    .then((frame) => {
      if (frame.status === 'OK' && frame.data) {
        setFrameGrab(frame);
      }
      // else {
      //   setFrameGrab(undefined);
      // }
      return frame;
    })
    .catch(showErrorDialog);
};
