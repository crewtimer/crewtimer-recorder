import { UseDatum } from 'react-usedatum';

import { UseStoredDatum } from '../store/UseElectronDatum';
import generateTestPattern from '../util/ImageUtils';
import {
  DefaultRecordingStatus,
  GrabFrameResponse,
  RecordingProps,
  RecordingStatus,
} from './RecorderTypes';

export const [useRecordingProps, , getRecordingProps] =
  UseStoredDatum<RecordingProps>('recording', {
    recordingFolder: './',
    recordingPrefix: 'CT_',
    recordingDuration: 5,
    networkCamera: '',
  });
export const [
  useRecordingStartTime,
  setRecordingStartTime,
  getRecordingStartTime,
] = UseDatum(0);
export const [useIsRecording, setIsRecording] = UseDatum(false);

export const [useRecordingStatus, setRecordingStatus] =
  UseDatum<RecordingStatus>(DefaultRecordingStatus);

export const [useFrameGrab, setFrameGrab] = UseDatum<
  GrabFrameResponse | undefined
>(generateTestPattern());

export const [useGuide, setGuide] = UseStoredDatum('guide', { pt1: 0, pt2: 0 });
