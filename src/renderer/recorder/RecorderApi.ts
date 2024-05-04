import { UseDatum } from 'react-usedatum';
import { HandlerResponse } from '../msgbus/MsgbusTypes';

import { UseStoredDatum } from '../store/UseElectronDatum';

interface RecordingProps {
  recordingFolder: string;
  recordingPrefix: string;
  recordingDuration: number;
}

export const [useRecordingProps, , getRecordingProps] =
  UseStoredDatum<RecordingProps>('recording', {
    recordingFolder: './',
    recordingPrefix: 'CT_',
    recordingDuration: 5,
  });
export const [useRecordingStartTime, , getRecordingStartTime] = UseDatum(0);
export const [useIsRecording, setIsRecording] = UseDatum(false);

export interface RecorderMessage {
  op:
    | 'start-recording'
    | 'stop-recording'
    | 'recording-status'
    | 'recording-log';
}

export interface StartRecorderMessage extends RecorderMessage {
  op: 'start-recording';
  props: {
    recordingFolder: string;
    recordingPrefix: string;
    recordingDuration: number;
  };
}
export interface RecorderResponse extends HandlerResponse {}

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

export interface RecordingStatus extends HandlerResponse {
  recording: boolean;
  recordingDuration: number;
  frameProcessor: {
    recording: boolean;
    error: string;
    filename: string;
    width: number;
    height: number;
    fps: number;
  };
}

export const DefaultRecordingStatus: RecordingStatus = {
  status: 'OK',
  error: '',
  recording: false,
  recordingDuration: 0,
  frameProcessor: {
    recording: false,
    error: '',
    filename: '',
    width: 0,
    height: 0,
    fps: 0,
  },
};
export const [useRecordingStatus, setRecordingStatus] =
  UseDatum<RecordingStatus>(DefaultRecordingStatus);

export const queryRecordingStatus = () => {
  return window.msgbus.sendMessage<RecorderMessage, RecordingStatus>(
    'recorder',
    {
      op: 'recording-status',
    },
  );
};

export interface RecordingLogEntry {
  tsMilli: number;
  subsystem: string;
  message: string;
}
export interface RecordingLog extends HandlerResponse {
  list: RecordingLogEntry[];
}

export const queryRecordingLog = () => {
  return window.msgbus.sendMessage<RecorderMessage, RecordingLog>('recorder', {
    op: 'recording-log',
  });
};
