import { HandlerResponse } from '../msgbus/MsgbusTypes';

export interface RecordingProps {
  recordingFolder: string;
  recordingPrefix: string;
  recordingDuration: number;
  networkCamera: string;
}

export interface RecorderMessage {
  op:
    | 'start-recording'
    | 'stop-recording'
    | 'recording-status'
    | 'recording-log'
    | 'grab-frame'
    | 'get-camera-list';
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
export interface GrabFrameResponse extends HandlerResponse {
  data: Buffer;
  width: number;
  height: number;
  totalBytes: number;
}

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

export interface RecordingLogEntry {
  tsMilli: number;
  subsystem: string;
  message: string;
}
export interface RecordingLog extends HandlerResponse {
  list: RecordingLogEntry[];
}

export interface CameraListResponse extends HandlerResponse {
  cameras: {
    name: string;
    address: string;
  }[];
}
