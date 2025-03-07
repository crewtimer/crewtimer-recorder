import { HandlerResponse } from '../msgbus/MsgbusTypes';
import { ViscaMessage, ViscaResponse } from './ViscaTypes';

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface RecordingProps {
  recordingFolder: string;
  recordingPrefix: string;
  recordingDuration: number;
  networkCamera: string;
  showFinishGuide: boolean;
  cropArea: Rect;
}

export interface RecorderMessage {
  op:
    | 'start-recording'
    | 'stop-recording'
    | 'recording-status'
    | 'recording-log'
    | 'grab-frame'
    | 'get-camera-list'
    | 'send-visca-cmd';
}

export interface StartRecorderMessage extends RecorderMessage {
  op: 'start-recording';
  props: {
    recordingFolder: string;
    recordingPrefix: string;
    recordingDuration: number;
    networkCamera: string;
    cropArea: Rect;
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
    frameBacklog: number;
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
    frameBacklog: 0,
  },
};

export const DefaultRecordingProps: RecordingProps = {
  recordingFolder: './',
  recordingPrefix: 'CT_',
  recordingDuration: 120,
  networkCamera: '',
  showFinishGuide: true,
  cropArea: { x: 0, y: 0, width: 1, height: 1 },
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

export type RecorderMessageTypes =
  | StartRecorderMessage
  | RecorderMessage
  | ViscaMessage;
export type RecorderMessageResponseType =
  | HandlerResponse
  | RecorderResponse
  | GrabFrameResponse
  | CameraListResponse
  | RecordingStatus
  | RecordingLog
  | ViscaResponse;
