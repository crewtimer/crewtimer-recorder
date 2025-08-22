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
  networkIP: string;
  showFinishGuide: boolean;
  cropArea: Rect;
  waypoint: string;
}

export interface RecorderMessage {
  op:
    | 'start-recording'
    | 'stop-recording'
    | 'recording-status'
    | 'recording-log'
    | 'grab-frame'
    | 'get-camera-list'
    | 'send-visca-cmd'
    | 'settings';
}

export interface SettingsMessage extends RecorderMessage {
  op: 'settings';
  props: { [key: string]: number | string };
}

export interface StartRecorderMessage extends RecorderMessage {
  op: 'start-recording';
  props: {
    recordingFolder: string;
    recordingPrefix: string;
    recordingDuration: number;
    networkCamera: string;
    cropArea: Rect;
    guide: { pt1: number; pt2: number };
    reportAllGaps?: boolean;
    addTimeOverlay?: boolean;
  };
}
export interface RecorderResponse extends HandlerResponse {}
export interface GrabFrameResponse extends HandlerResponse {
  data: Buffer;
  width: number;
  height: number;
  totalBytes: number;
  tsMilli: number;
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
    lastTsMilli: number;
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
    lastTsMilli: 0,
  },
};

export const DefaultRecordingProps: RecordingProps = {
  recordingFolder: './',
  recordingPrefix: 'CT_',
  recordingDuration: 120,
  networkCamera: '',
  networkIP: '',
  showFinishGuide: true,
  cropArea: { x: 0, y: 0, width: 1, height: 1 },
  waypoint: '',
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
  | ViscaMessage
  | SettingsMessage;
export type RecorderMessageResponseType =
  | HandlerResponse
  | RecorderResponse
  | GrabFrameResponse
  | CameraListResponse
  | RecordingStatus
  | RecordingLog
  | ViscaResponse;
